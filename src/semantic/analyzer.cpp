#include "semantic/analyzer.hpp"

#include <cstdint>
#include <unordered_set>
#include <utility>

namespace nurt {

namespace {

/// Renders a type for diagnostics, e.g. "int (#)" or "void".
[[nodiscard]] std::string typeDisplay(Type type) {
    std::string result(type_name(type));
    if (type != Type::Void) {
        result += " (";
        result += type_sigil(type);
        result += ')';
    }
    return result;
}

/// Renders a sigil-prefixed variable for diagnostics, e.g. "#wynik".
[[nodiscard]] std::string variableDisplay(Type type, std::string_view name) {
    std::string result(1, type_sigil(type));
    result += name;
    return result;
}

[[nodiscard]] bool isArithmetic(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add:
    case BinaryOp::Subtract:
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
    case BinaryOp::Modulo:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isEquality(BinaryOp op) {
    return op == BinaryOp::Equal || op == BinaryOp::NotEqual;
}

[[nodiscard]] bool isOrdering(BinaryOp op) {
    switch (op) {
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
        return true;
    default:
        return false;
    }
}

} // namespace

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

bool SemanticAnalyzer::analyze(const ProgramNode& program) {
    functions_.clear();
    globals_.clear();
    locals_.clear();
    currentFunction_ = nullptr;
    errorCount_ = 0;

    analyzeBlock(program.statements);
    return errorCount_ == 0;
}

SemanticAnalyzer::VariableScope& SemanticAnalyzer::currentScope() {
    return currentFunction_ != nullptr ? locals_ : globals_;
}

void SemanticAnalyzer::error(SourceLocation location, std::string message) {
    diagnostics_.error(location, std::move(message));
    ++errorCount_;
}

void SemanticAnalyzer::note(SourceLocation location, std::string message) {
    diagnostics_.note(location, std::move(message));
}

// --- Statements --------------------------------------------------------------------

void SemanticAnalyzer::analyzeBlock(const StatementList& statements) {
    for (const StmtPtr& statement : statements) {
        analyzeStatement(*statement);
    }
}

void SemanticAnalyzer::analyzeStatement(const StatementNode& statement) {
    switch (statement.kind) {
    case NodeKind::FunctionDef:
        analyzeFunctionDef(static_cast<const FunctionDefNode&>(statement));
        break;
    case NodeKind::Assignment:
        analyzeAssignment(static_cast<const AssignmentNode&>(statement));
        break;
    case NodeKind::ExpressionStatement:
        analyzeExpressionStatement(static_cast<const ExpressionStatementNode&>(statement));
        break;
    case NodeKind::Return:
        analyzeReturn(static_cast<const ReturnNode&>(statement));
        break;
    case NodeKind::IfStatement:
        analyzeIf(static_cast<const IfStatementNode&>(statement));
        break;
    case NodeKind::MatchStatement:
        analyzeMatch(static_cast<const MatchStatementNode&>(statement));
        break;
    case NodeKind::While:
        analyzeWhile(static_cast<const WhileNode&>(statement));
        break;
    default:
        break;
    }
}

void SemanticAnalyzer::analyzeFunctionDef(const FunctionDefNode& node) {
    if (currentFunction_ != nullptr) {
        error(node.location, "'powolaj' is only allowed at the top level; function '" + node.name +
                                 "' cannot be defined inside another function");
        return;
    }

    if (node.name == "pisz" || node.name == "bierz") {
        error(node.location, "cannot redefine the built-in function '" + node.name + "'");
        return;
    }

    if (const auto existing = functions_.find(node.name); existing != functions_.end()) {
        error(node.location, "function '" + node.name + "' is already defined");
        note(existing->second.declaredAt, "previous definition of '" + node.name + "' is here");
        return;
    }

    // Register the signature before the body so direct recursion type-checks.
    FunctionSymbol symbol;
    symbol.returnType = node.returnType;
    symbol.declaredAt = node.location;
    symbol.parameterTypes.reserve(node.parameters.size());
    for (const Parameter& parameter : node.parameters) {
        symbol.parameterTypes.push_back(parameter.type);
    }
    functions_.emplace(node.name, std::move(symbol));

    currentFunction_ = &node;
    locals_.clear();
    for (const Parameter& parameter : node.parameters) {
        const auto [it, inserted] =
            locals_.emplace(parameter.name, VariableSymbol{parameter.type, parameter.location});
        if (!inserted) {
            error(parameter.location, "duplicate parameter name '" + parameter.name +
                                          "' in function '" + node.name + "'");
            note(it->second.declaredAt, "first parameter named '" + parameter.name + "' is here");
        }
    }

    analyzeBlock(node.body);

    if (node.returnType != Type::Void && !blockAlwaysReturns(node.body)) {
        error(node.location, "function '" + node.name + "' is declared to return " +
                                 typeDisplay(node.returnType) +
                                 " but does not return a value on every control path");
    }

    currentFunction_ = nullptr;
    locals_.clear();
}

void SemanticAnalyzer::analyzeAssignment(const AssignmentNode& node) {
    // 'bierz()' has no type of its own: it reads whatever the assignment
    // target's sigil demands, so it is only valid in this direct position.
    if (node.value->kind == NodeKind::Call &&
        static_cast<const CallNode&>(*node.value).builtin == BuiltinKind::Bierz) {
        const auto& call = static_cast<const CallNode&>(*node.value);
        if (!call.arguments.empty()) {
            error(call.location, "'bierz' takes no arguments");
        }
        if (node.targetType == Type::Bool) {
            error(call.location, "'bierz' can read an int (#) or a string ($), not a bool (?)");
        }
        declareOrCheckTarget(node);
        return;
    }

    const std::optional<Type> valueType =
        requireValue(typeOf(*node.value), node.value->location, "the assigned expression");
    if (valueType && *valueType != node.targetType) {
        error(node.targetLocation,
              "type mismatch: " + typeDisplay(*valueType) + " value cannot flow into '" +
                  variableDisplay(node.targetType, node.targetName) + "' (" +
                  typeDisplay(node.targetType) + ")");
    }

    // Declare the target even after an error so later uses of the variable do
    // not produce a cascade of "used before assignment" reports.
    declareOrCheckTarget(node);
}

void SemanticAnalyzer::declareOrCheckTarget(const AssignmentNode& node) {
    VariableScope& scope = currentScope();
    const auto existing = scope.find(node.targetName);
    if (existing == scope.end()) {
        scope.emplace(node.targetName, VariableSymbol{node.targetType, node.targetLocation});
        return;
    }
    if (existing->second.type != node.targetType) {
        error(node.targetLocation,
              "'" + node.targetName + "' was declared as '" +
                  variableDisplay(existing->second.type, node.targetName) + "' (" +
                  typeDisplay(existing->second.type) + "); it cannot be reassigned as '" +
                  variableDisplay(node.targetType, node.targetName) + "'");
        note(existing->second.declaredAt, "'" + node.targetName + "' first received a value here");
    }
}

void SemanticAnalyzer::analyzeExpressionStatement(const ExpressionStatementNode& node) {
    const std::optional<Type> type = typeOf(*node.expression);
    if (type && *type != Type::Void) {
        diagnostics_.warning(node.location, "the " + typeDisplay(*type) +
                                                " result of this expression is ignored; "
                                                "use '-> " +
                                                std::string(1, type_sigil(*type)) +
                                                "nazwa' to keep it");
    }
}

void SemanticAnalyzer::analyzeReturn(const ReturnNode& node) {
    if (currentFunction_ == nullptr) {
        error(node.location, "'<-' return is only allowed inside a 'powolaj' function");
        if (node.value) {
            (void)typeOf(*node.value);
        }
        return;
    }

    const Type expected = currentFunction_->returnType;

    if (!node.value) {
        if (expected != Type::Void) {
            error(node.location, "function '" + currentFunction_->name + "' must return " +
                                     typeDisplay(expected) + "; write '<- wyrazenie'");
        }
        return;
    }

    if (expected == Type::Void) {
        (void)typeOf(*node.value);
        error(node.value->location, "void function '" + currentFunction_->name +
                                        "' cannot return a value (it has no '->' "
                                        "return type annotation)");
        return;
    }

    const std::optional<Type> actual =
        requireValue(typeOf(*node.value), node.value->location, "the returned expression");
    if (actual && *actual != expected) {
        error(node.value->location, "return type mismatch: function '" + currentFunction_->name +
                                        "' returns " + typeDisplay(expected) + ", got " +
                                        typeDisplay(*actual));
    }
}

void SemanticAnalyzer::analyzeIf(const IfStatementNode& node) {
    checkCondition(*node.condition, "'?' query");
    analyzeBlock(node.trueBranch);
    analyzeBlock(node.falseBranch);
}

void SemanticAnalyzer::analyzeMatch(const MatchStatementNode& node) {
    const std::optional<Type> targetType =
        requireValue(typeOf(*node.target), node.target->location, "the 'dopasuj' target");

    std::unordered_set<std::int64_t> seenInts;
    std::unordered_set<std::string> seenStrings;
    bool seenPrawda = false;
    bool seenFalsz = false;

    for (const MatchCase& matchCase : node.cases) {
        const ExpressionNode& literal = *matchCase.literal;
        const std::optional<Type> literalType = typeOf(literal);  // literals never fail

        if (targetType && literalType && *literalType != *targetType) {
            error(matchCase.location, "'dopasuj' case type mismatch: the target is " +
                                          typeDisplay(*targetType) + ", but this case literal is " +
                                          typeDisplay(*literalType));
        }

        switch (literal.kind) {
        case NodeKind::IntegerLiteral: {
            const auto value = static_cast<const IntegerLiteralNode&>(literal).value;
            if (!seenInts.insert(value).second) {
                error(matchCase.location,
                      "duplicate 'dopasuj' case '" + std::to_string(value) + "'");
            }
            break;
        }
        case NodeKind::StringLiteral: {
            const auto& value = static_cast<const StringLiteralNode&>(literal).value;
            if (!seenStrings.insert(value).second) {
                error(matchCase.location, "duplicate 'dopasuj' case '\"" + value + "\"'");
            }
            break;
        }
        case NodeKind::BoolLiteral: {
            const bool value = static_cast<const BoolLiteralNode&>(literal).value;
            bool& seen = value ? seenPrawda : seenFalsz;
            if (seen) {
                error(matchCase.location, std::string("duplicate 'dopasuj' case '") +
                                              (value ? "prawda" : "falsz") + "'");
            }
            seen = true;
            break;
        }
        default:
            break;
        }

        analyzeBlock(matchCase.body);
    }

    analyzeBlock(node.defaultBranch);
}

void SemanticAnalyzer::analyzeWhile(const WhileNode& node) {
    checkCondition(*node.condition, "'dopoki' loop");
    analyzeBlock(node.body);
}

void SemanticAnalyzer::checkCondition(const ExpressionNode& condition,
                                      std::string_view constructName) {
    const std::optional<Type> type =
        requireValue(typeOf(condition), condition.location, "the condition");
    if (type && *type != Type::Bool) {
        error(condition.location, "the " + std::string(constructName) +
                                      " condition must be a bool (?), got " + typeDisplay(*type));
    }
}

// --- Expressions ----------------------------------------------------------------------

std::optional<Type> SemanticAnalyzer::typeOf(const ExpressionNode& expression) {
    switch (expression.kind) {
    case NodeKind::IntegerLiteral:
        return Type::Int;
    case NodeKind::StringLiteral:
        return Type::String;
    case NodeKind::BoolLiteral:
        return Type::Bool;
    case NodeKind::Variable:
        return typeOfVariable(static_cast<const VariableNode&>(expression));
    case NodeKind::Unary:
        return typeOfUnary(static_cast<const UnaryNode&>(expression));
    case NodeKind::Binary:
        return typeOfBinary(static_cast<const BinaryNode&>(expression));
    case NodeKind::Call:
        return typeOfCall(static_cast<const CallNode&>(expression));
    default:
        return std::nullopt;
    }
}

std::optional<Type> SemanticAnalyzer::typeOfVariable(const VariableNode& node) {
    const VariableScope& scope = currentScope();
    const auto it = scope.find(node.name);
    if (it == scope.end()) {
        error(node.location, "variable '" + variableDisplay(node.type, node.name) +
                                 "' is used before a value flows into it");
        if (currentFunction_ != nullptr && globals_.contains(node.name)) {
            note(node.location, "global variables are not visible inside functions; pass '" +
                                    node.name + "' to '" + currentFunction_->name +
                                    "' as a parameter instead");
        }
        return std::nullopt;
    }
    if (it->second.type != node.type) {
        error(node.location, "'" + node.name + "' was declared as '" +
                                 variableDisplay(it->second.type, node.name) + "' (" +
                                 typeDisplay(it->second.type) + "); it cannot be referenced as '" +
                                 variableDisplay(node.type, node.name) + "'");
        note(it->second.declaredAt, "'" + node.name + "' first received a value here");
        return std::nullopt;
    }
    return it->second.type;
}

std::optional<Type> SemanticAnalyzer::typeOfUnary(const UnaryNode& node) {
    const std::optional<Type> operand =
        requireValue(typeOf(*node.operand), node.operand->location, "the operand");
    if (!operand) {
        return std::nullopt;
    }

    if (node.op == UnaryOp::Negate) {
        if (*operand != Type::Int) {
            error(node.location,
                  "unary '-' requires an int (#) operand, got " + typeDisplay(*operand));
            return std::nullopt;
        }
        return Type::Int;
    }

    if (*operand != Type::Bool) {
        error(node.location, "'!' requires a bool (?) operand, got " + typeDisplay(*operand));
        return std::nullopt;
    }
    return Type::Bool;
}

std::optional<Type> SemanticAnalyzer::typeOfBinary(const BinaryNode& node) {
    // Type both sides up front so one bad operand does not hide errors in the
    // other.
    const std::optional<Type> lhs =
        requireValue(typeOf(*node.lhs), node.lhs->location, "the left operand");
    const std::optional<Type> rhs =
        requireValue(typeOf(*node.rhs), node.rhs->location, "the right operand");
    if (!lhs || !rhs) {
        return std::nullopt;
    }

    const std::string_view opName = binary_op_name(node.op);

    if (isArithmetic(node.op)) {
        if (*lhs != Type::Int || *rhs != Type::Int) {
            error(node.location, "operator '" + std::string(opName) +
                                     "' requires int (#) operands, got " + typeDisplay(*lhs) +
                                     " and " + typeDisplay(*rhs));
            return std::nullopt;
        }
        return Type::Int;
    }

    if (isOrdering(node.op)) {
        if (*lhs != Type::Int || *rhs != Type::Int) {
            error(node.location, "operator '" + std::string(opName) +
                                     "' requires int (#) operands, got " + typeDisplay(*lhs) +
                                     " and " + typeDisplay(*rhs));
            return std::nullopt;
        }
        return Type::Bool;
    }

    if (isEquality(node.op)) {
        if (*lhs != *rhs) {
            error(node.location, "operator '" + std::string(opName) +
                                     "' requires operands of the same type, got " +
                                     typeDisplay(*lhs) + " and " + typeDisplay(*rhs));
            return std::nullopt;
        }
        return Type::Bool;
    }

    // 'i' and 'lub'.
    if (*lhs != Type::Bool || *rhs != Type::Bool) {
        error(node.location, "operator '" + std::string(opName) +
                                 "' requires bool (?) operands, got " + typeDisplay(*lhs) +
                                 " and " + typeDisplay(*rhs));
        return std::nullopt;
    }
    return Type::Bool;
}

std::optional<Type> SemanticAnalyzer::typeOfCall(const CallNode& node) {
    if (node.builtin == BuiltinKind::Pisz) {
        if (node.arguments.empty()) {
            error(node.location, "'pisz' requires at least one argument");
        }
        for (const ExprPtr& argument : node.arguments) {
            (void)requireValue(typeOf(*argument), argument->location, "a 'pisz' argument");
        }
        return Type::Void;
    }

    if (node.builtin == BuiltinKind::Bierz) {
        // Valid 'bierz()' uses are intercepted by analyzeAssignment.
        error(node.location,
              "the result of 'bierz' must flow directly into a variable: 'bierz() -> #x'");
        return std::nullopt;
    }

    const auto it = functions_.find(node.callee);
    if (it == functions_.end()) {
        std::string message = "call to undefined function '" + node.callee + "'";
        if (node.callee == "out") {
            message += "; did you mean the built-in 'pisz'?";
        } else if (node.callee == "in") {
            message += "; did you mean the built-in 'bierz'?";
        }
        error(node.location, std::move(message));
        // Still type-check the arguments to surface their own errors.
        for (const ExprPtr& argument : node.arguments) {
            (void)typeOf(*argument);
        }
        return std::nullopt;
    }

    const FunctionSymbol& callee = it->second;
    if (node.arguments.size() != callee.parameterTypes.size()) {
        error(node.location, "function '" + node.callee + "' expects " +
                                 std::to_string(callee.parameterTypes.size()) + " argument(s), " +
                                 std::to_string(node.arguments.size()) + " given");
        note(callee.declaredAt, "'" + node.callee + "' is defined here");
        for (const ExprPtr& argument : node.arguments) {
            (void)typeOf(*argument);
        }
        return callee.returnType;
    }

    for (std::size_t i = 0; i < node.arguments.size(); ++i) {
        const std::optional<Type> argumentType =
            requireValue(typeOf(*node.arguments[i]), node.arguments[i]->location, "an argument");
        if (argumentType && *argumentType != callee.parameterTypes[i]) {
            error(node.arguments[i]->location,
                  "argument " + std::to_string(i + 1) + " of '" + node.callee + "' must be " +
                      typeDisplay(callee.parameterTypes[i]) + ", got " +
                      typeDisplay(*argumentType));
        }
    }
    return callee.returnType;
}

std::optional<Type> SemanticAnalyzer::requireValue(std::optional<Type> type,
                                                   SourceLocation location,
                                                   std::string_view what) {
    if (type && *type == Type::Void) {
        error(location, std::string(what) + " has no value: it is a call to a void function");
        return std::nullopt;
    }
    return type;
}

// --- Return path analysis -----------------------------------------------------------------

bool SemanticAnalyzer::blockAlwaysReturns(const StatementList& statements) {
    for (const StmtPtr& statement : statements) {
        if (statementAlwaysReturns(*statement)) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::statementAlwaysReturns(const StatementNode& statement) {
    switch (statement.kind) {
    case NodeKind::Return:
        return true;
    case NodeKind::IfStatement: {
        const auto& node = static_cast<const IfStatementNode&>(statement);
        // Only a query with BOTH branches present can guarantee a return; an
        // absent branch falls through. A 'dopoki' body may never execute, so
        // loops never guarantee a return.
        return !node.trueBranch.empty() && !node.falseBranch.empty() &&
               blockAlwaysReturns(node.trueBranch) && blockAlwaysReturns(node.falseBranch);
    }
    case NodeKind::MatchStatement: {
        const auto& node = static_cast<const MatchStatementNode&>(statement);
        // 'dopasuj' guarantees a return only when every case body AND the
        // mandatory 'inaczej' branch guarantee one; the cases are never
        // assumed exhaustive, the default covers the rest.
        for (const MatchCase& matchCase : node.cases) {
            if (!blockAlwaysReturns(matchCase.body)) {
                return false;
            }
        }
        return blockAlwaysReturns(node.defaultBranch);
    }
    default:
        return false;
    }
}

} // namespace nurt
