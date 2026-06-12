#pragma once

#include "common/diagnostics.hpp"
#include "parser/ast.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nurt {

/// A variable known to the analyzer. Variables are declared implicitly by the
/// first assignment ('10 -> #x'); the sigil of that first assignment fixes the
/// type for the variable's whole lifetime.
struct VariableSymbol {
    Type type = Type::Int;
    SourceLocation declaredAt;
};

/// A user-defined function registered from its 'powolaj' definition.
struct FunctionSymbol {
    std::vector<Type> parameterTypes;
    Type returnType = Type::Void;
    SourceLocation declaredAt;
};

/// Semantic analysis pass over the parsed AST.
///
/// Checks performed:
///  - **Scopes & symbols**: variables must receive a value (via assignment or
///    as a parameter) before they are read; functions must be defined before
///    they are called (which still permits direct recursion); function bodies
///    see only their parameters and locals, never global variables; 'powolaj'
///    is only allowed at the top level; function and parameter names must be
///    unique; the built-ins 'pisz'/'bierz' cannot be redefined.
///  - **Sigil consistency**: the sigil of the first assignment fixes a
///    variable's type; later uses or assignments with a different sigil are
///    errors.
///  - **Expression typing**: arithmetic ('+ - * / %') and ordering
///    ('< <= > >=') require int operands; equality ('==' '!=') requires both
///    operands to share one type; '&&'/'||'/'!' require bool; unary '-'
///    requires int. Void call results cannot be used as values.
///  - **Statement typing**: an assigned expression must match the target
///    sigil; 'dopoki' and '?' query conditions must be bool; '<-' must match
///    the enclosing function's declared return type; non-void functions must
///    return on every control path.
///  - **Built-ins**: 'pisz(...)' takes one or more non-void arguments and
///    yields no value; 'bierz()' takes no arguments and must flow directly
///    into an int or string variable ('bierz() -> #x'), which gives it its
///    type.
///
/// All problems are reported through the DiagnosticEngine with precise source
/// locations. The analyzer keeps going after an error and avoids cascading
/// reports by treating already-failed subexpressions as silently unknown.
class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticEngine& diagnostics);

    /// Runs all checks over the program. Returns true when no semantic error
    /// was found (pre-existing diagnostics are not counted).
    bool analyze(const ProgramNode& program);

private:
    using VariableScope = std::unordered_map<std::string, VariableSymbol>;

    // --- Statements -----------------------------------------------------------
    void analyzeBlock(const StatementList& statements);
    void analyzeStatement(const StatementNode& statement);
    void analyzeFunctionDef(const FunctionDefNode& node);
    void analyzeAssignment(const AssignmentNode& node);
    void analyzeExpressionStatement(const ExpressionStatementNode& node);
    void analyzeReturn(const ReturnNode& node);
    void analyzeIf(const IfStatementNode& node);
    void analyzeWhile(const WhileNode& node);

    /// Type-checks a 'dopoki' or '?' query condition; it must be bool.
    void checkCondition(const ExpressionNode& condition, std::string_view constructName);

    /// Declares the assignment target on first use, or verifies the sigil
    /// matches the earlier declaration.
    void declareOrCheckTarget(const AssignmentNode& node);

    // --- Expressions ------------------------------------------------------------
    /// Computes the static type of an expression. Returns nullopt when a
    /// diagnostic was already emitted for a subexpression, so callers stay
    /// silent and cascades are avoided. Type::Void is a valid result (a call
    /// to a void function); most contexts must reject it explicitly.
    [[nodiscard]] std::optional<Type> typeOf(const ExpressionNode& expression);
    [[nodiscard]] std::optional<Type> typeOfVariable(const VariableNode& node);
    [[nodiscard]] std::optional<Type> typeOfUnary(const UnaryNode& node);
    [[nodiscard]] std::optional<Type> typeOfBinary(const BinaryNode& node);
    [[nodiscard]] std::optional<Type> typeOfCall(const CallNode& node);

    /// Rejects void where a value is required. Returns the type unchanged
    /// when it is usable, nullopt (with a diagnostic) when it is void.
    std::optional<Type> requireValue(std::optional<Type> type, SourceLocation location,
                                     std::string_view what);

    // --- Return path analysis ------------------------------------------------------
    [[nodiscard]] static bool blockAlwaysReturns(const StatementList& statements);
    [[nodiscard]] static bool statementAlwaysReturns(const StatementNode& statement);

    // --- State ------------------------------------------------------------------------
    [[nodiscard]] VariableScope& currentScope();

    void error(SourceLocation location, std::string message);
    void note(SourceLocation location, std::string message);

    DiagnosticEngine& diagnostics_;
    std::unordered_map<std::string, FunctionSymbol> functions_;
    VariableScope globals_;
    VariableScope locals_;
    const FunctionDefNode* currentFunction_ = nullptr;
    std::size_t errorCount_ = 0;  ///< errors emitted by this pass only
};

} // namespace nurt
