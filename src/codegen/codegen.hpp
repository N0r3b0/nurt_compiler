#pragma once

#include "common/diagnostics.hpp"
#include "parser/ast.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nurt {

/// x86_64 Linux code generator emitting NASM (Intel syntax) assembly.
///
/// Targets the System V AMD64 ABI and links against libc ('printf', 'scanf',
/// 'strcmp'); the produced file assembles with 'nasm -f elf64' and links with
/// 'gcc -no-pie' (see scripts/wypusc.sh).
///
/// Lowering model (deliberately simple and deterministic, suited for a
/// validated AST - run the SemanticAnalyzer first):
///  - Every Nurt value is a 64-bit qword: ints are i64, bools are 0/1, and
///    strings are pointers to NUL-terminated bytes in '.rodata' (literals) or
///    '.bss' (one 256-byte buffer per 'bierz() -> $x' call site).
///  - Top-level code becomes the body of 'main'; top-level variables are its
///    stack locals. Each user function 'f' is emitted as 'nurt_f' (the prefix
///    avoids collisions with libc symbols).
///  - Stack frames: 'push rbp; mov rbp, rsp; push rbx; sub rsp, 8*locals'.
///    Every variable (parameters included) gets a fixed slot '[rbp - offset]'
///    assigned in order of first appearance; '[rbp - 8]' holds the saved rbx.
///  - Expressions evaluate into 'rax'. Binary operators evaluate the left
///    operand, push it, evaluate the right operand, then pop into 'rbx'.
///    Assignment stores 'rax' into the target's slot.
///  - Every 'call' is wrapped in a dynamic 16-byte realignment of 'rsp'
///    (saved in callee-preserved 'rbx'), so alignment holds regardless of how
///    many operands are pushed at that moment.
///  - '?' queries lower to 'cmp rax, 0' + 'je' with '.prawda_N/.falsz_N/
///    .koniec_N' labels; 'dopoki' loops use '.dopoki_N/.wyjscie_N'.
///  - 'pisz' prints each argument with printf by static type ('%lld', '%s',
///    or the words 'prawda'/'falsz'); 'bierz' scanfs '%lld' into the target
///    slot's address or '%255s' into the call site's buffer.
///  - '&&'/'||' are bitwise on 0/1 values (no short-circuit; operands are
///    already restricted to bools by the analyzer).
///
/// The only structural limit is six parameters/arguments per function (the
/// six System V integer argument registers); exceeding it is reported through
/// the DiagnosticEngine.
class CodeGenerator {
public:
    explicit CodeGenerator(DiagnosticEngine& diagnostics);

    /// Generates the complete NASM source for a semantically valid program.
    /// Check DiagnosticEngine::hasErrors() afterwards; on error the returned
    /// text is incomplete and must not be assembled.
    [[nodiscard]] std::string generate(const ProgramNode& program);

private:
    // --- Emission helpers ----------------------------------------------------
    void line(std::string_view instruction);
    void comment(std::string_view text);
    void label(std::string_view name);

    /// Realigns rsp to 16 bytes (saving it in rbx), emits the call, restores
    /// rsp. 'variadic' additionally zeroes al as the SysV vararg convention
    /// requires for printf/scanf.
    void emitAlignedCall(std::string_view callee, bool variadic);

    // --- Functions and frames ----------------------------------------------------
    /// Emits one function: 'function' is null for 'main' (top-level code).
    void emitFunction(const FunctionDefNode* function, const StatementList& body,
                      std::string_view symbol);
    void emitEpilogue();

    /// Assigns a stack slot (on first sight) and records the variable's type.
    void declareVariable(const std::string& name, Type type);
    /// Walks a body and pre-declares every assignment target, so slot offsets
    /// are deterministic before any code is emitted.
    void collectLocals(const StatementList& statements);
    void collectLocalsIn(const StatementNode& statement);
    [[nodiscard]] std::string slotOf(const std::string& name) const;

    // --- Statements ------------------------------------------------------------------
    void emitBlock(const StatementList& statements);
    void emitStatement(const StatementNode& statement);
    void emitAssignment(const AssignmentNode& node);
    void emitBierzAssignment(const AssignmentNode& node, const CallNode& call);
    void emitReturn(const ReturnNode& node);
    void emitIf(const IfStatementNode& node);
    void emitWhile(const WhileNode& node);

    // --- Expressions --------------------------------------------------------------------
    /// Emits code leaving the expression's value in rax.
    void emitExpression(const ExpressionNode& expression);
    void emitBinary(const BinaryNode& node);
    void emitCall(const CallNode& node);
    void emitPisz(const CallNode& node);

    /// Static type of a validated expression (mirrors the analyzer's rules).
    [[nodiscard]] Type exprType(const ExpressionNode& expression) const;

    // --- Data ------------------------------------------------------------------------------
    /// Interns a string constant in .rodata and returns its label.
    std::string stringLabel(std::string_view value);
    /// Reserves a .bss read buffer for one 'bierz() -> $x' call site.
    std::string reserveReadBuffer();

    // --- State ------------------------------------------------------------------------------
    DiagnosticEngine& diagnostics_;

    std::ostringstream text_;
    std::ostringstream rodata_;
    std::ostringstream bss_;

    std::unordered_map<std::string, std::string> internedStrings_;
    std::unordered_map<std::string, Type> functionReturnTypes_;

    // Per-function state (reset by emitFunction).
    std::unordered_map<std::string, std::size_t> slotOffsets_;
    std::unordered_map<std::string, Type> variableTypes_;

    int nextLabelId_ = 0;
    int nextStringId_ = 0;
    int nextBufferId_ = 0;
};

} // namespace nurt
