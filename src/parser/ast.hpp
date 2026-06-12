#pragma once

#include "common/source_location.hpp"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace nurt {

/// Static types of Nurt values, fixed by the sigil of a variable reference
/// ('#' int, '$' string, '?' bool). Void exists only as a function return type.
enum class Type {
    Int,
    String,
    Bool,
    Void,
};

[[nodiscard]] constexpr std::string_view type_name(Type type) {
    switch (type) {
    case Type::Int:
        return "int";
    case Type::String:
        return "string";
    case Type::Bool:
        return "bool";
    case Type::Void:
        return "void";
    }
    return "<invalid>";
}

/// The sigil character that denotes the type in source code.
[[nodiscard]] constexpr char type_sigil(Type type) {
    switch (type) {
    case Type::Int:
        return '#';
    case Type::String:
        return '$';
    case Type::Bool:
        return '?';
    case Type::Void:
        return ' ';
    }
    return '?';
}

enum class NodeKind {
    // --- Expressions ----------------------------------------------------------
    IntegerLiteral,
    StringLiteral,
    BoolLiteral,
    Variable,
    Unary,
    Binary,
    Call,

    // --- Statements -----------------------------------------------------------
    Assignment,
    ExpressionStatement,
    Return,
    IfStatement,
    While,
    FunctionDef,

    // --- Root -----------------------------------------------------------------
    Program,
};

/// Base of every AST node. Nodes are identified by `kind` (no RTTI needed)
/// and carry the source location of their first token for diagnostics.
struct Node {
    NodeKind kind;
    SourceLocation location;

    virtual ~Node() = default;

protected:
    Node(NodeKind nodeKind, SourceLocation nodeLocation)
        : kind(nodeKind), location(nodeLocation) {}
};

struct ExpressionNode : Node {
protected:
    using Node::Node;
};

struct StatementNode : Node {
protected:
    using Node::Node;
};

using ExprPtr = std::unique_ptr<ExpressionNode>;
using StmtPtr = std::unique_ptr<StatementNode>;
using StatementList = std::vector<StmtPtr>;

// --- Expressions ---------------------------------------------------------------

struct IntegerLiteralNode final : ExpressionNode {
    IntegerLiteralNode(std::int64_t literalValue, SourceLocation loc)
        : ExpressionNode(NodeKind::IntegerLiteral, loc), value(literalValue) {}

    std::int64_t value;
};

struct StringLiteralNode final : ExpressionNode {
    StringLiteralNode(std::string literalValue, SourceLocation loc)
        : ExpressionNode(NodeKind::StringLiteral, loc), value(std::move(literalValue)) {}

    /// Decoded value (escape sequences already resolved by the lexer).
    std::string value;
};

struct BoolLiteralNode final : ExpressionNode {
    BoolLiteralNode(bool literalValue, SourceLocation loc)
        : ExpressionNode(NodeKind::BoolLiteral, loc), value(literalValue) {}

    bool value;
};

/// A sigil-prefixed variable reference such as '#x', '$tekst', or '?flaga'.
struct VariableNode final : ExpressionNode {
    VariableNode(Type variableType, std::string variableName, SourceLocation loc)
        : ExpressionNode(NodeKind::Variable, loc), type(variableType),
          name(std::move(variableName)) {}

    Type type;
    std::string name;
};

enum class UnaryOp {
    Negate,  ///< '-'
    Not,     ///< '!'
};

[[nodiscard]] constexpr std::string_view unary_op_name(UnaryOp op) {
    switch (op) {
    case UnaryOp::Negate:
        return "-";
    case UnaryOp::Not:
        return "!";
    }
    return "<invalid>";
}

struct UnaryNode final : ExpressionNode {
    UnaryNode(UnaryOp unaryOp, ExprPtr unaryOperand, SourceLocation loc)
        : ExpressionNode(NodeKind::Unary, loc), op(unaryOp), operand(std::move(unaryOperand)) {}

    UnaryOp op;
    ExprPtr operand;
};

enum class BinaryOp {
    Add,           ///< '+'
    Subtract,      ///< '-'
    Multiply,      ///< '*'
    Divide,        ///< '/'
    Modulo,        ///< '%'
    Equal,         ///< '=='
    NotEqual,      ///< '!='
    Less,          ///< '<'
    LessEqual,     ///< '<='
    Greater,       ///< '>'
    GreaterEqual,  ///< '>='
    And,           ///< '&&'
    Or,            ///< '||'
};

[[nodiscard]] constexpr std::string_view binary_op_name(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add:
        return "+";
    case BinaryOp::Subtract:
        return "-";
    case BinaryOp::Multiply:
        return "*";
    case BinaryOp::Divide:
        return "/";
    case BinaryOp::Modulo:
        return "%";
    case BinaryOp::Equal:
        return "==";
    case BinaryOp::NotEqual:
        return "!=";
    case BinaryOp::Less:
        return "<";
    case BinaryOp::LessEqual:
        return "<=";
    case BinaryOp::Greater:
        return ">";
    case BinaryOp::GreaterEqual:
        return ">=";
    case BinaryOp::And:
        return "&&";
    case BinaryOp::Or:
        return "||";
    }
    return "<invalid>";
}

struct BinaryNode final : ExpressionNode {
    BinaryNode(BinaryOp binaryOp, ExprPtr left, ExprPtr right, SourceLocation loc)
        : ExpressionNode(NodeKind::Binary, loc), op(binaryOp), lhs(std::move(left)),
          rhs(std::move(right)) {}

    BinaryOp op;
    ExprPtr lhs;
    ExprPtr rhs;
};

/// Built-in functions handled directly by later compiler stages.
enum class BuiltinKind {
    None,   ///< an ordinary user-defined function
    Pisz,   ///< 'pisz(...)'  - write to standard output
    Bierz,  ///< 'bierz()'    - read from standard input
};

/// A call expression: 'suma(#x, 32)', 'pisz($powitanie)', 'bierz()'.
struct CallNode final : ExpressionNode {
    CallNode(std::string calleeName, BuiltinKind builtinKind, std::vector<ExprPtr> callArguments,
             SourceLocation loc)
        : ExpressionNode(NodeKind::Call, loc), callee(std::move(calleeName)), builtin(builtinKind),
          arguments(std::move(callArguments)) {}

    std::string callee;
    BuiltinKind builtin;
    std::vector<ExprPtr> arguments;
};

// --- Statements ----------------------------------------------------------------

/// Nurt's inverted assignment: '[expression] -> #target'. The value is parsed
/// first and flows left-to-right into the target variable.
struct AssignmentNode final : StatementNode {
    AssignmentNode(ExprPtr assignedValue, Type type, std::string name,
                   SourceLocation targetLoc, SourceLocation loc)
        : StatementNode(NodeKind::Assignment, loc), value(std::move(assignedValue)),
          targetType(type), targetName(std::move(name)), targetLocation(targetLoc) {}

    ExprPtr value;
    Type targetType;
    std::string targetName;
    SourceLocation targetLocation;
};

/// An expression evaluated for its side effects, e.g. 'pisz("hej")'.
struct ExpressionStatementNode final : StatementNode {
    ExpressionStatementNode(ExprPtr expr, SourceLocation loc)
        : StatementNode(NodeKind::ExpressionStatement, loc), expression(std::move(expr)) {}

    ExprPtr expression;
};

/// '<- [expression]'. The expression is null for a bare '<-' (void return).
struct ReturnNode final : StatementNode {
    ReturnNode(ExprPtr returnValue, SourceLocation loc)
        : StatementNode(NodeKind::Return, loc), value(std::move(returnValue)) {}

    ExprPtr value;
};

/// The control flow query:
///   [condition] ?
///   | prawda =>:
///       [statements]
///   | falsz =>:
///       [statements]
///   koniec
/// Either branch may be omitted (but at least one must be present).
struct IfStatementNode final : StatementNode {
    IfStatementNode(ExprPtr queryCondition, StatementList prawdaBranch, StatementList falszBranch,
                    SourceLocation loc)
        : StatementNode(NodeKind::IfStatement, loc), condition(std::move(queryCondition)),
          trueBranch(std::move(prawdaBranch)), falseBranch(std::move(falszBranch)) {}

    ExprPtr condition;
    StatementList trueBranch;
    StatementList falseBranch;
};

/// 'dopoki [condition] rob: [statements] koniec'
struct WhileNode final : StatementNode {
    WhileNode(ExprPtr loopCondition, StatementList loopBody, SourceLocation loc)
        : StatementNode(NodeKind::While, loc), condition(std::move(loopCondition)),
          body(std::move(loopBody)) {}

    ExprPtr condition;
    StatementList body;
};

struct Parameter {
    Type type = Type::Int;
    std::string name;
    SourceLocation location;
};

/// 'powolaj name(params) -> #: [statements] koniec'. The return type
/// annotation ('-> #:' / '-> $:' / '-> ?:') is omitted for void functions.
struct FunctionDefNode final : StatementNode {
    FunctionDefNode(std::string functionName, std::vector<Parameter> functionParams,
                    Type functionReturnType, StatementList functionBody, SourceLocation loc)
        : StatementNode(NodeKind::FunctionDef, loc), name(std::move(functionName)),
          parameters(std::move(functionParams)), returnType(functionReturnType),
          body(std::move(functionBody)) {}

    std::string name;
    std::vector<Parameter> parameters;
    Type returnType;
    StatementList body;
};

// --- Root ------------------------------------------------------------------------

struct ProgramNode final : Node {
    explicit ProgramNode(SourceLocation loc) : Node(NodeKind::Program, loc) {}

    StatementList statements;
};

/// Pretty-prints the AST as an indented tree (used by 'nurtc --ast' and tests).
void dump_ast(const ProgramNode& program, std::ostream& out);

} // namespace nurt
