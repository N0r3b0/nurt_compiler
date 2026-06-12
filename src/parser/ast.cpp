#include "parser/ast.hpp"

#include <string>

namespace nurt {

namespace {

class AstPrinter {
public:
    explicit AstPrinter(std::ostream& out) : out_(out) {}

    void print(const ProgramNode& program) {
        line() << "Program\n";
        Indent guard(*this);
        for (const StmtPtr& statement : program.statements) {
            printStatement(*statement);
        }
    }

private:
    struct Indent {
        explicit Indent(AstPrinter& printer) : printer_(printer) { ++printer_.depth_; }
        ~Indent() { --printer_.depth_; }
        AstPrinter& printer_;
    };

    std::ostream& line() {
        for (int i = 0; i < depth_; ++i) {
            out_ << "  ";
        }
        return out_;
    }

    static std::string escaped(std::string_view raw) {
        std::string result;
        result.reserve(raw.size());
        for (const char c : raw) {
            switch (c) {
            case '\n':
                result += "\\n";
                break;
            case '\t':
                result += "\\t";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\0':
                result += "\\0";
                break;
            default:
                result.push_back(c);
                break;
            }
        }
        return result;
    }

    void printStatement(const StatementNode& statement) {
        switch (statement.kind) {
        case NodeKind::Assignment: {
            const auto& node = static_cast<const AssignmentNode&>(statement);
            line() << "Assignment -> " << type_sigil(node.targetType) << node.targetName << '\n';
            Indent guard(*this);
            printExpression(*node.value);
            break;
        }
        case NodeKind::ExpressionStatement: {
            const auto& node = static_cast<const ExpressionStatementNode&>(statement);
            line() << "ExpressionStatement\n";
            Indent guard(*this);
            printExpression(*node.expression);
            break;
        }
        case NodeKind::Return: {
            const auto& node = static_cast<const ReturnNode&>(statement);
            line() << "Return" << (node.value ? "" : " (void)") << '\n';
            if (node.value) {
                Indent guard(*this);
                printExpression(*node.value);
            }
            break;
        }
        case NodeKind::IfStatement: {
            const auto& node = static_cast<const IfStatementNode&>(statement);
            line() << "IfStatement\n";
            Indent guard(*this);
            line() << "Condition\n";
            {
                Indent conditionGuard(*this);
                printExpression(*node.condition);
            }
            printBranch("PrawdaBranch", node.trueBranch);
            printBranch("FalszBranch", node.falseBranch);
            break;
        }
        case NodeKind::While: {
            const auto& node = static_cast<const WhileNode&>(statement);
            line() << "While\n";
            Indent guard(*this);
            line() << "Condition\n";
            {
                Indent conditionGuard(*this);
                printExpression(*node.condition);
            }
            printBranch("Body", node.body);
            break;
        }
        case NodeKind::FunctionDef: {
            const auto& node = static_cast<const FunctionDefNode&>(statement);
            line() << "FunctionDef " << node.name << '(';
            bool first = true;
            for (const Parameter& parameter : node.parameters) {
                if (!first) {
                    out_ << ", ";
                }
                first = false;
                out_ << type_sigil(parameter.type) << parameter.name;
            }
            out_ << ") -> " << type_name(node.returnType) << '\n';
            Indent guard(*this);
            for (const StmtPtr& bodyStatement : node.body) {
                printStatement(*bodyStatement);
            }
            break;
        }
        default:
            line() << "<unknown statement>\n";
            break;
        }
    }

    void printBranch(std::string_view label, const StatementList& statements) {
        if (statements.empty()) {
            return;
        }
        line() << label << '\n';
        Indent guard(*this);
        for (const StmtPtr& statement : statements) {
            printStatement(*statement);
        }
    }

    void printExpression(const ExpressionNode& expression) {
        switch (expression.kind) {
        case NodeKind::IntegerLiteral: {
            const auto& node = static_cast<const IntegerLiteralNode&>(expression);
            line() << "IntegerLiteral " << node.value << '\n';
            break;
        }
        case NodeKind::StringLiteral: {
            const auto& node = static_cast<const StringLiteralNode&>(expression);
            line() << "StringLiteral \"" << escaped(node.value) << "\"\n";
            break;
        }
        case NodeKind::BoolLiteral: {
            const auto& node = static_cast<const BoolLiteralNode&>(expression);
            line() << "BoolLiteral " << (node.value ? "prawda" : "falsz") << '\n';
            break;
        }
        case NodeKind::Variable: {
            const auto& node = static_cast<const VariableNode&>(expression);
            line() << "Variable " << type_sigil(node.type) << node.name << '\n';
            break;
        }
        case NodeKind::Unary: {
            const auto& node = static_cast<const UnaryNode&>(expression);
            line() << "Unary " << unary_op_name(node.op) << '\n';
            Indent guard(*this);
            printExpression(*node.operand);
            break;
        }
        case NodeKind::Binary: {
            const auto& node = static_cast<const BinaryNode&>(expression);
            line() << "Binary " << binary_op_name(node.op) << '\n';
            Indent guard(*this);
            printExpression(*node.lhs);
            printExpression(*node.rhs);
            break;
        }
        case NodeKind::Call: {
            const auto& node = static_cast<const CallNode&>(expression);
            line() << "Call " << node.callee;
            if (node.builtin == BuiltinKind::Pisz) {
                out_ << " [builtin pisz]";
            } else if (node.builtin == BuiltinKind::Bierz) {
                out_ << " [builtin bierz]";
            }
            out_ << '\n';
            Indent guard(*this);
            for (const ExprPtr& argument : node.arguments) {
                printExpression(*argument);
            }
            break;
        }
        default:
            line() << "<unknown expression>\n";
            break;
        }
    }

    std::ostream& out_;
    int depth_ = 0;
};

} // namespace

void dump_ast(const ProgramNode& program, std::ostream& out) {
    AstPrinter printer(out);
    printer.print(program);
}

} // namespace nurt
