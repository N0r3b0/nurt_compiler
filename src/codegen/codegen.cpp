#include "codegen/codegen.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nurt {

namespace {

constexpr std::array<std::string_view, 6> kArgumentRegisters = {"rdi", "rsi", "rdx",
                                                                "rcx", "r8",  "r9"};

/// Renders a string as a NASM 'db' directive: printable runs are quoted,
/// everything else (and the quote character) is emitted numerically. Always
/// NUL-terminated.
[[nodiscard]] std::string dbDirective(std::string_view value) {
    std::string out = "db ";
    bool first = true;
    const auto separator = [&] {
        if (!first) {
            out += ", ";
        }
        first = false;
    };

    std::size_t i = 0;
    while (i < value.size()) {
        const char c = value[i];
        if (c >= 32 && c <= 126 && c != '\'') {
            separator();
            out += '\'';
            while (i < value.size() && value[i] >= 32 && value[i] <= 126 && value[i] != '\'') {
                out += value[i];
                ++i;
            }
            out += '\'';
        } else {
            separator();
            out += std::to_string(static_cast<unsigned char>(c));
            ++i;
        }
    }
    separator();
    out += '0';
    return out;
}

[[nodiscard]] std::string mangled(std::string_view functionName) {
    return "nurt_" + std::string(functionName);
}

/// setCC mnemonic for an int/bool comparison operator.
[[nodiscard]] std::string_view comparisonSet(BinaryOp op) {
    switch (op) {
    case BinaryOp::Equal:
        return "sete";
    case BinaryOp::NotEqual:
        return "setne";
    case BinaryOp::Less:
        return "setl";
    case BinaryOp::LessEqual:
        return "setle";
    case BinaryOp::Greater:
        return "setg";
    case BinaryOp::GreaterEqual:
        return "setge";
    default:
        return "sete";
    }
}

} // namespace

CodeGenerator::CodeGenerator(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

std::string CodeGenerator::generate(const ProgramNode& program) {
    // Pass 1: function return types, needed to type call expressions.
    for (const StmtPtr& statement : program.statements) {
        if (statement->kind == NodeKind::FunctionDef) {
            const auto& function = static_cast<const FunctionDefNode&>(*statement);
            functionReturnTypes_.emplace(function.name, function.returnType);
        }
    }

    // Pass 2: emit every function, then top-level code as 'main'.
    std::vector<const StatementNode*> mainBody;
    for (const StmtPtr& statement : program.statements) {
        if (statement->kind == NodeKind::FunctionDef) {
            const auto& function = static_cast<const FunctionDefNode&>(*statement);
            emitFunction(&function, function.body, mangled(function.name));
        } else {
            mainBody.push_back(statement.get());
        }
    }

    slotOffsets_.clear();
    variableTypes_.clear();
    for (const StatementNode* statement : mainBody) {
        collectLocalsIn(*statement);
    }

    text_ << "\n; kod najwyzszego poziomu\n";
    label("main");
    line("push rbp");
    line("mov rbp, rsp");
    line("push rbx");
    if (!slotOffsets_.empty()) {
        line("sub rsp, " + std::to_string(8 * slotOffsets_.size()));
    }
    for (const StatementNode* statement : mainBody) {
        emitStatement(*statement);
    }
    comment("program zakonczony pomyslnie");
    line("xor eax, eax");
    emitEpilogue();

    // Assemble the final file.
    std::ostringstream out;
    out << "; --- wygenerowane przez nurtc: Nurt -> x86_64 NASM (System V AMD64) ---\n"
        << "bits 64\n"
        << "default rel\n"
        << "\n"
        << "global main\n"
        << "extern printf\n"
        << "extern scanf\n"
        << "extern strcmp\n"
        << "\n"
        << "section .text\n"
        << text_.str();

    const std::string rodata = rodata_.str();
    if (!rodata.empty()) {
        out << "\nsection .rodata\n" << rodata;
    }
    const std::string bss = bss_.str();
    if (!bss.empty()) {
        out << "\nsection .bss\n" << bss;
    }
    return out.str();
}

// --- Emission helpers ---------------------------------------------------------------

void CodeGenerator::line(std::string_view instruction) {
    text_ << "    " << instruction << '\n';
}

void CodeGenerator::comment(std::string_view textContent) {
    text_ << "    ; " << textContent << '\n';
}

void CodeGenerator::label(std::string_view name) {
    text_ << name << ":\n";
}

void CodeGenerator::emitAlignedCall(std::string_view callee, bool variadic) {
    line("mov rbx, rsp");
    line("and rsp, -16");
    if (variadic) {
        line("xor eax, eax");
    }
    line("call " + std::string(callee));
    line("mov rsp, rbx");
}

// --- Functions and frames ------------------------------------------------------------

void CodeGenerator::emitFunction(const FunctionDefNode* function, const StatementList& body,
                                 std::string_view symbol) {
    slotOffsets_.clear();
    variableTypes_.clear();

    if (function != nullptr && function->parameters.size() > kArgumentRegisters.size()) {
        diagnostics_.error(function->location,
                           "function '" + function->name + "' has " +
                               std::to_string(function->parameters.size()) +
                               " parameters; the x86_64 backend supports at most " +
                               std::to_string(kArgumentRegisters.size()));
        return;
    }

    if (function != nullptr) {
        for (const Parameter& parameter : function->parameters) {
            declareVariable(parameter.name, parameter.type);
        }
    }
    collectLocals(body);

    text_ << '\n';
    if (function != nullptr) {
        text_ << "; powolaj " << function->name << " (" << function->parameters.size()
              << " parametry) -> " << type_name(function->returnType) << '\n';
    }
    label(symbol);
    line("push rbp");
    line("mov rbp, rsp");
    line("push rbx");
    if (!slotOffsets_.empty()) {
        line("sub rsp, " + std::to_string(8 * slotOffsets_.size()));
    }

    if (function != nullptr) {
        for (std::size_t i = 0; i < function->parameters.size(); ++i) {
            const Parameter& parameter = function->parameters[i];
            comment("parametr " + std::string(1, type_sigil(parameter.type)) + parameter.name);
            line("mov " + slotOf(parameter.name) + ", " + std::string(kArgumentRegisters[i]));
        }
    }

    emitBlock(body);

    // Void functions (and any non-void path the analyzer proved unreachable)
    // fall through to a final epilogue.
    emitEpilogue();
}

void CodeGenerator::emitEpilogue() {
    line("lea rsp, [rbp - 8]");
    line("pop rbx");
    line("pop rbp");
    line("ret");
}

void CodeGenerator::declareVariable(const std::string& name, Type type) {
    if (slotOffsets_.contains(name)) {
        return;
    }
    // [rbp - 8] is the saved rbx; locals start at [rbp - 16].
    const std::size_t offset = 16 + 8 * slotOffsets_.size();
    slotOffsets_.emplace(name, offset);
    variableTypes_.emplace(name, type);
}

void CodeGenerator::collectLocals(const StatementList& statements) {
    for (const StmtPtr& statement : statements) {
        collectLocalsIn(*statement);
    }
}

void CodeGenerator::collectLocalsIn(const StatementNode& statement) {
    switch (statement.kind) {
    case NodeKind::Assignment: {
        const auto& node = static_cast<const AssignmentNode&>(statement);
        declareVariable(node.targetName, node.targetType);
        break;
    }
    case NodeKind::IfStatement: {
        const auto& node = static_cast<const IfStatementNode&>(statement);
        collectLocals(node.trueBranch);
        collectLocals(node.falseBranch);
        break;
    }
    case NodeKind::MatchStatement: {
        const auto& node = static_cast<const MatchStatementNode&>(statement);
        for (const MatchCase& matchCase : node.cases) {
            collectLocals(matchCase.body);
        }
        collectLocals(node.defaultBranch);
        break;
    }
    case NodeKind::While: {
        const auto& node = static_cast<const WhileNode&>(statement);
        collectLocals(node.body);
        break;
    }
    default:
        break;
    }
}

std::string CodeGenerator::slotOf(const std::string& name) const {
    return "qword [rbp - " + std::to_string(slotOffsets_.at(name)) + "]";
}

// --- Statements ----------------------------------------------------------------------------

void CodeGenerator::emitBlock(const StatementList& statements) {
    for (const StmtPtr& statement : statements) {
        emitStatement(*statement);
    }
}

void CodeGenerator::emitStatement(const StatementNode& statement) {
    switch (statement.kind) {
    case NodeKind::Assignment:
        emitAssignment(static_cast<const AssignmentNode&>(statement));
        break;
    case NodeKind::ExpressionStatement:
        emitExpression(*static_cast<const ExpressionStatementNode&>(statement).expression);
        break;
    case NodeKind::Return:
        emitReturn(static_cast<const ReturnNode&>(statement));
        break;
    case NodeKind::IfStatement:
        emitIf(static_cast<const IfStatementNode&>(statement));
        break;
    case NodeKind::MatchStatement:
        emitMatch(static_cast<const MatchStatementNode&>(statement));
        break;
    case NodeKind::While:
        emitWhile(static_cast<const WhileNode&>(statement));
        break;
    default:
        break;
    }
}

void CodeGenerator::emitAssignment(const AssignmentNode& node) {
    comment("-> " + std::string(1, type_sigil(node.targetType)) + node.targetName);

    if (node.value->kind == NodeKind::Call) {
        const auto& call = static_cast<const CallNode&>(*node.value);
        if (call.builtin == BuiltinKind::Bierz) {
            emitBierzAssignment(node, call);
            return;
        }
    }

    emitExpression(*node.value);
    line("mov " + slotOf(node.targetName) + ", rax");
}

void CodeGenerator::emitBierzAssignment(const AssignmentNode& node, const CallNode& call) {
    (void)call;
    if (node.targetType == Type::Int) {
        // scanf("%lld", &slot)
        line("lea rsi, [rbp - " + std::to_string(slotOffsets_.at(node.targetName)) + "]");
        line("lea rdi, [" + stringLabel("%lld") + "]");
        emitAlignedCall("scanf", /*variadic=*/true);
        return;
    }

    // scanf("%255s", buffer); slot = buffer
    const std::string buffer = reserveReadBuffer();
    line("lea rsi, [" + buffer + "]");
    line("lea rdi, [" + stringLabel("%255s") + "]");
    emitAlignedCall("scanf", /*variadic=*/true);
    line("lea rax, [" + buffer + "]");
    line("mov " + slotOf(node.targetName) + ", rax");
}

void CodeGenerator::emitReturn(const ReturnNode& node) {
    comment("<-");
    if (node.value) {
        emitExpression(*node.value);
    }
    emitEpilogue();
}

void CodeGenerator::emitIf(const IfStatementNode& node) {
    const int id = nextLabelId_++;
    const std::string prawda = ".prawda_" + std::to_string(id);
    const std::string falsz = ".falsz_" + std::to_string(id);
    const std::string koniec = ".koniec_" + std::to_string(id);

    comment("? zapytanie " + std::to_string(id));
    emitExpression(*node.condition);
    line("cmp rax, 0");
    line("je " + falsz);
    label(prawda);
    emitBlock(node.trueBranch);
    line("jmp " + koniec);
    label(falsz);
    emitBlock(node.falseBranch);
    label(koniec);
}

void CodeGenerator::emitMatch(const MatchStatementNode& node) {
    const int id = nextLabelId_++;
    const std::string inaczej = ".inaczej_" + std::to_string(id);
    const std::string koniec = ".koniec_dopasuj_" + std::to_string(id);
    const auto caseLabel = [id](std::size_t k) {
        return ".przypadek_" + std::to_string(id) + "_" + std::to_string(k);
    };

    const Type targetType = exprType(*node.target);

    comment("dopasuj " + std::to_string(id));
    emitExpression(*node.target);

    const bool isString = targetType == Type::String;
    if (isString) {
        // Park the target pointer on the stack: strcmp clobbers rax, and each
        // case must compare against the original target.
        line("push rax");
    }

    // Dispatch chain: one test per case, falling through to 'inaczej'.
    for (std::size_t k = 0; k < node.cases.size(); ++k) {
        const ExpressionNode& literal = *node.cases[k].literal;
        if (isString) {
            const auto& value = static_cast<const StringLiteralNode&>(literal).value;
            line("mov rdi, [rsp]");
            line("lea rsi, [" + stringLabel(value) + "]");
            emitAlignedCall("strcmp", /*variadic=*/false);
            line("cmp eax, 0");
            line("je " + caseLabel(k));
            continue;
        }

        std::int64_t value = 0;
        if (literal.kind == NodeKind::IntegerLiteral) {
            value = static_cast<const IntegerLiteralNode&>(literal).value;
        } else {
            value = static_cast<const BoolLiteralNode&>(literal).value ? 1 : 0;
        }
        if (value >= INT32_MIN && value <= INT32_MAX) {
            line("cmp rax, " + std::to_string(value));
        } else {
            // 'cmp r64, imm' only takes a sign-extended 32-bit immediate.
            line("mov rcx, " + std::to_string(value));
            line("cmp rax, rcx");
        }
        line("je " + caseLabel(k));
    }
    line("jmp " + inaczej);

    // Case bodies. String matching parked the target with 'push'; every body
    // is entered through exactly one label, so it is dropped exactly once.
    for (std::size_t k = 0; k < node.cases.size(); ++k) {
        label(caseLabel(k));
        if (isString) {
            line("add rsp, 8");
        }
        emitBlock(node.cases[k].body);
        line("jmp " + koniec);
    }

    label(inaczej);
    if (isString) {
        line("add rsp, 8");
    }
    emitBlock(node.defaultBranch);
    label(koniec);
}

void CodeGenerator::emitWhile(const WhileNode& node) {
    const int id = nextLabelId_++;
    const std::string start = ".dopoki_" + std::to_string(id);
    const std::string exit = ".wyjscie_" + std::to_string(id);

    label(start);
    comment("dopoki - warunek " + std::to_string(id));
    emitExpression(*node.condition);
    line("cmp rax, 0");
    line("je " + exit);
    emitBlock(node.body);
    line("jmp " + start);
    label(exit);
}

// --- Expressions ------------------------------------------------------------------------------

void CodeGenerator::emitExpression(const ExpressionNode& expression) {
    switch (expression.kind) {
    case NodeKind::IntegerLiteral:
        line("mov rax, " +
             std::to_string(static_cast<const IntegerLiteralNode&>(expression).value));
        break;
    case NodeKind::StringLiteral:
        line("lea rax, [" +
             stringLabel(static_cast<const StringLiteralNode&>(expression).value) + "]");
        break;
    case NodeKind::BoolLiteral:
        line(static_cast<const BoolLiteralNode&>(expression).value ? "mov rax, 1"
                                                                   : "mov rax, 0");
        break;
    case NodeKind::Variable:
        line("mov rax, " + slotOf(static_cast<const VariableNode&>(expression).name));
        break;
    case NodeKind::Unary: {
        const auto& node = static_cast<const UnaryNode&>(expression);
        emitExpression(*node.operand);
        line(node.op == UnaryOp::Negate ? "neg rax" : "xor rax, 1");
        break;
    }
    case NodeKind::Binary:
        emitBinary(static_cast<const BinaryNode&>(expression));
        break;
    case NodeKind::Call:
        emitCall(static_cast<const CallNode&>(expression));
        break;
    default:
        break;
    }
}

void CodeGenerator::emitBinary(const BinaryNode& node) {
    emitExpression(*node.lhs);
    line("push rax");
    emitExpression(*node.rhs);
    line("pop rbx");
    // Invariant from here: lhs in rbx, rhs in rax.

    switch (node.op) {
    case BinaryOp::Add:
        line("add rax, rbx");
        break;
    case BinaryOp::Subtract:
        line("sub rbx, rax");
        line("mov rax, rbx");
        break;
    case BinaryOp::Multiply:
        line("imul rax, rbx");
        break;
    case BinaryOp::Divide:
    case BinaryOp::Modulo:
        line("mov rcx, rax");
        line("mov rax, rbx");
        line("cqo");
        line("idiv rcx");
        if (node.op == BinaryOp::Modulo) {
            line("mov rax, rdx");
        }
        break;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
        if (exprType(*node.lhs) == Type::String) {
            // Content comparison via strcmp, not pointer identity.
            line("mov rdi, rbx");
            line("mov rsi, rax");
            emitAlignedCall("strcmp", /*variadic=*/false);
            line("cmp eax, 0");
            line(std::string(node.op == BinaryOp::Equal ? "sete" : "setne") + " al");
            line("movzx rax, al");
            break;
        }
        [[fallthrough]];
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
        line("cmp rbx, rax");
        line(std::string(comparisonSet(node.op)) + " al");
        line("movzx rax, al");
        break;
    case BinaryOp::And:
        line("and rax, rbx");
        break;
    case BinaryOp::Or:
        line("or rax, rbx");
        break;
    }
}

void CodeGenerator::emitCall(const CallNode& node) {
    if (node.builtin == BuiltinKind::Pisz) {
        emitPisz(node);
        return;
    }
    if (node.builtin == BuiltinKind::Bierz) {
        // Unreachable in a validated AST: 'bierz' is handled by emitAssignment.
        return;
    }

    if (node.arguments.size() > kArgumentRegisters.size()) {
        diagnostics_.error(node.location,
                           "call to '" + node.callee + "' passes " +
                               std::to_string(node.arguments.size()) +
                               " arguments; the x86_64 backend supports at most " +
                               std::to_string(kArgumentRegisters.size()));
        return;
    }

    for (const ExprPtr& argument : node.arguments) {
        emitExpression(*argument);
        line("push rax");
    }
    for (std::size_t i = node.arguments.size(); i-- > 0;) {
        line("pop " + std::string(kArgumentRegisters[i]));
    }
    emitAlignedCall(mangled(node.callee), /*variadic=*/false);
}

void CodeGenerator::emitPisz(const CallNode& node) {
    for (const ExprPtr& argument : node.arguments) {
        emitExpression(*argument);
        switch (exprType(*argument)) {
        case Type::Int:
            line("mov rsi, rax");
            line("lea rdi, [" + stringLabel("%lld") + "]");
            break;
        case Type::Bool:
            line("lea rsi, [" + stringLabel("prawda") + "]");
            line("lea rcx, [" + stringLabel("falsz") + "]");
            line("test rax, rax");
            line("cmove rsi, rcx");
            line("lea rdi, [" + stringLabel("%s") + "]");
            break;
        default:  // Type::String
            line("mov rsi, rax");
            line("lea rdi, [" + stringLabel("%s") + "]");
            break;
        }
        emitAlignedCall("printf", /*variadic=*/true);
    }
}

Type CodeGenerator::exprType(const ExpressionNode& expression) const {
    switch (expression.kind) {
    case NodeKind::IntegerLiteral:
        return Type::Int;
    case NodeKind::StringLiteral:
        return Type::String;
    case NodeKind::BoolLiteral:
        return Type::Bool;
    case NodeKind::Variable:
        return static_cast<const VariableNode&>(expression).type;
    case NodeKind::Unary:
        return static_cast<const UnaryNode&>(expression).op == UnaryOp::Negate ? Type::Int
                                                                               : Type::Bool;
    case NodeKind::Binary: {
        switch (static_cast<const BinaryNode&>(expression).op) {
        case BinaryOp::Add:
        case BinaryOp::Subtract:
        case BinaryOp::Multiply:
        case BinaryOp::Divide:
        case BinaryOp::Modulo:
            return Type::Int;
        default:
            return Type::Bool;
        }
    }
    case NodeKind::Call: {
        const auto& call = static_cast<const CallNode&>(expression);
        if (call.builtin != BuiltinKind::None) {
            return Type::Void;
        }
        const auto it = functionReturnTypes_.find(call.callee);
        return it != functionReturnTypes_.end() ? it->second : Type::Void;
    }
    default:
        return Type::Void;
    }
}

// --- Data ----------------------------------------------------------------------------------------

std::string CodeGenerator::stringLabel(std::string_view value) {
    const std::string key(value);
    const auto it = internedStrings_.find(key);
    if (it != internedStrings_.end()) {
        return it->second;
    }
    const std::string name = "napis_" + std::to_string(nextStringId_++);
    rodata_ << name << ": " << dbDirective(value) << '\n';
    internedStrings_.emplace(key, name);
    return name;
}

std::string CodeGenerator::reserveReadBuffer() {
    const std::string name = "bufor_bierz_" + std::to_string(nextBufferId_++);
    bss_ << name << ": resb 256\n";
    return name;
}

} // namespace nurt
