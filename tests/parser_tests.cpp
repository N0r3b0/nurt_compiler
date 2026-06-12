#include "parser/parser.hpp"

#include "lexer/lexer.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <vector>

namespace {

using nurt::AssignmentNode;
using nurt::BinaryNode;
using nurt::BinaryOp;
using nurt::BoolLiteralNode;
using nurt::BuiltinKind;
using nurt::CallNode;
using nurt::ExpressionStatementNode;
using nurt::FunctionDefNode;
using nurt::IfStatementNode;
using nurt::IntegerLiteralNode;
using nurt::ReturnNode;
using nurt::StringLiteralNode;
using nurt::Type;
using nurt::UnaryNode;
using nurt::UnaryOp;
using nurt::VariableNode;
using nurt::WhileNode;

struct ParseResult {
    std::unique_ptr<nurt::ProgramNode> program;
    std::vector<nurt::Diagnostic> diagnostics;
    std::size_t errorCount = 0;
};

/// Lexes and parses `source` to completion. NOTE: pass string literals (or
/// otherwise keep the buffer alive) - tokens and diagnostics view into it.
ParseResult parse(std::string_view source) {
    nurt::DiagnosticEngine diagnostics("test.nrt", source);
    nurt::Lexer lexer(source, diagnostics);
    nurt::Parser parser(lexer.tokenize(), diagnostics);
    ParseResult result;
    result.program = parser.parseProgram();
    result.diagnostics = diagnostics.diagnostics();
    result.errorCount = diagnostics.errorCount();
    return result;
}

/// Downcasts a node and fails the test (returning nullptr) on a kind mismatch.
template <typename T, typename Ptr>
const T* as(const Ptr& node) {
    return dynamic_cast<const T*>(node.get());
}

} // namespace

// --- Arrow assignment ----------------------------------------------------------

TEST(ParserAssignment, BindsExpressionToTarget) {
    const ParseResult result = parse("10 -> #x");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);

    const auto* assignment = as<AssignmentNode>(result.program->statements[0]);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->targetType, Type::Int);
    EXPECT_EQ(assignment->targetName, "x");

    const auto* value = as<IntegerLiteralNode>(assignment->value);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, 10);
}

TEST(ParserAssignment, AllSigilTargets) {
    const ParseResult result = parse(
        "42 -> #liczba\n"
        "\"tekst\" -> $napis\n"
        "falsz -> ?flaga\n");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 3u);

    const auto* first = as<AssignmentNode>(result.program->statements[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->targetType, Type::Int);

    const auto* second = as<AssignmentNode>(result.program->statements[1]);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->targetType, Type::String);
    const auto* secondValue = as<StringLiteralNode>(second->value);
    ASSERT_NE(secondValue, nullptr);
    EXPECT_EQ(secondValue->value, "tekst");

    const auto* third = as<AssignmentNode>(result.program->statements[2]);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third->targetType, Type::Bool);
    const auto* thirdValue = as<BoolLiteralNode>(third->value);
    ASSERT_NE(thirdValue, nullptr);
    EXPECT_FALSE(thirdValue->value);
}

TEST(ParserAssignment, WholeExpressionIsParsedBeforeArrow) {
    const ParseResult result = parse("#a + #b * 2 -> #wynik");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);

    const auto* assignment = as<AssignmentNode>(result.program->statements[0]);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->targetName, "wynik");

    // '#a + (#b * 2)' - the full expression flows into the target.
    const auto* sum = as<BinaryNode>(assignment->value);
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->op, BinaryOp::Add);
    const auto* product = as<BinaryNode>(sum->rhs);
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(product->op, BinaryOp::Multiply);
}

TEST(ParserAssignment, TargetMustBeSigilVariable) {
    const ParseResult result = parse("5 -> 6");
    EXPECT_GT(result.errorCount, 0u);
}

// --- Operator precedence and associativity ------------------------------------------

TEST(ParserExpressions, MultiplicationBindsTighterThanAddition) {
    const ParseResult result = parse("1 + 2 * 3 -> #x");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* assignment = as<AssignmentNode>(result.program->statements.at(0));
    ASSERT_NE(assignment, nullptr);
    const auto* root = as<BinaryNode>(assignment->value);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->op, BinaryOp::Add);

    const auto* lhs = as<IntegerLiteralNode>(root->lhs);
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->value, 1);

    const auto* rhs = as<BinaryNode>(root->rhs);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryOp::Multiply);
}

TEST(ParserExpressions, ArithmeticIsLeftAssociative) {
    const ParseResult result = parse("10 - 4 - 3 -> #x");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* assignment = as<AssignmentNode>(result.program->statements.at(0));
    ASSERT_NE(assignment, nullptr);

    // (10 - 4) - 3
    const auto* outer = as<BinaryNode>(assignment->value);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, BinaryOp::Subtract);
    const auto* inner = as<BinaryNode>(outer->lhs);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->op, BinaryOp::Subtract);
    const auto* last = as<IntegerLiteralNode>(outer->rhs);
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(last->value, 3);
}

TEST(ParserExpressions, ComparisonBindsLooserThanArithmetic) {
    const ParseResult result = parse("#a + 1 < #b * 2 -> ?wynik");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* assignment = as<AssignmentNode>(result.program->statements.at(0));
    ASSERT_NE(assignment, nullptr);
    const auto* comparison = as<BinaryNode>(assignment->value);
    ASSERT_NE(comparison, nullptr);
    EXPECT_EQ(comparison->op, BinaryOp::Less);
    EXPECT_EQ(as<BinaryNode>(comparison->lhs)->op, BinaryOp::Add);
    EXPECT_EQ(as<BinaryNode>(comparison->rhs)->op, BinaryOp::Multiply);
}

TEST(ParserExpressions, LogicalAndBindsTighterThanOr) {
    const ParseResult result = parse("?a lub ?b i ?c -> ?x");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* assignment = as<AssignmentNode>(result.program->statements.at(0));
    ASSERT_NE(assignment, nullptr);

    // ?a lub (?b i ?c)
    const auto* orNode = as<BinaryNode>(assignment->value);
    ASSERT_NE(orNode, nullptr);
    EXPECT_EQ(orNode->op, BinaryOp::Or);
    const auto* andNode = as<BinaryNode>(orNode->rhs);
    ASSERT_NE(andNode, nullptr);
    EXPECT_EQ(andNode->op, BinaryOp::And);
}

TEST(ParserExpressions, EqualityBindsLooserThanComparison) {
    const ParseResult result = parse("#a < #b == #c > #d -> ?x");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* assignment = as<AssignmentNode>(result.program->statements.at(0));
    ASSERT_NE(assignment, nullptr);

    // (#a < #b) == (#c > #d)
    const auto* equality = as<BinaryNode>(assignment->value);
    ASSERT_NE(equality, nullptr);
    EXPECT_EQ(equality->op, BinaryOp::Equal);
    EXPECT_EQ(as<BinaryNode>(equality->lhs)->op, BinaryOp::Less);
    EXPECT_EQ(as<BinaryNode>(equality->rhs)->op, BinaryOp::Greater);
}

TEST(ParserExpressions, ParenthesesOverridePrecedence) {
    const ParseResult result = parse("(1 + 2) * 3 -> #x");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* assignment = as<AssignmentNode>(result.program->statements.at(0));
    ASSERT_NE(assignment, nullptr);
    const auto* product = as<BinaryNode>(assignment->value);
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(product->op, BinaryOp::Multiply);
    EXPECT_EQ(as<BinaryNode>(product->lhs)->op, BinaryOp::Add);
}

TEST(ParserExpressions, UnaryOperators) {
    const ParseResult result = parse("-#x + 5 -> #y\n!?flaga -> ?nie");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 2u);

    // Unary '-' binds tighter than '+': (-#x) + 5.
    const auto* first = as<AssignmentNode>(result.program->statements[0]);
    ASSERT_NE(first, nullptr);
    const auto* sum = as<BinaryNode>(first->value);
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->op, BinaryOp::Add);
    const auto* negation = as<UnaryNode>(sum->lhs);
    ASSERT_NE(negation, nullptr);
    EXPECT_EQ(negation->op, UnaryOp::Negate);

    const auto* second = as<AssignmentNode>(result.program->statements[1]);
    ASSERT_NE(second, nullptr);
    const auto* notNode = as<UnaryNode>(second->value);
    ASSERT_NE(notNode, nullptr);
    EXPECT_EQ(notNode->op, UnaryOp::Not);
    const auto* operand = as<VariableNode>(notNode->operand);
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(operand->type, Type::Bool);
    EXPECT_EQ(operand->name, "flaga");
}

// --- Control flow query ----------------------------------------------------------------

TEST(ParserQuery, BothBranches) {
    const ParseResult result = parse(
        "?gotowe ?\n"
        "| prawda =>:\n"
        "    1 -> #x\n"
        "| falsz =>:\n"
        "    2 -> #x\n"
        "    3 -> #y\n"
        "koniec\n");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);

    const auto* query = as<IfStatementNode>(result.program->statements[0]);
    ASSERT_NE(query, nullptr);

    const auto* condition = as<VariableNode>(query->condition);
    ASSERT_NE(condition, nullptr);
    EXPECT_EQ(condition->type, Type::Bool);
    EXPECT_EQ(condition->name, "gotowe");

    EXPECT_EQ(query->trueBranch.size(), 1u);
    EXPECT_EQ(query->falseBranch.size(), 2u);
}

TEST(ParserQuery, SinglePrawdaBranch) {
    const ParseResult result = parse(
        "#x > 0 ?\n"
        "| prawda =>:\n"
        "    pisz(\"dodatnie\")\n"
        "koniec\n");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);

    const auto* query = as<IfStatementNode>(result.program->statements[0]);
    ASSERT_NE(query, nullptr);

    // The condition may be any boolean expression, not just a variable.
    const auto* condition = as<BinaryNode>(query->condition);
    ASSERT_NE(condition, nullptr);
    EXPECT_EQ(condition->op, BinaryOp::Greater);

    EXPECT_EQ(query->trueBranch.size(), 1u);
    EXPECT_TRUE(query->falseBranch.empty());
}

TEST(ParserQuery, BranchOrderIsFree) {
    const ParseResult result = parse(
        "?a ?\n"
        "| falsz =>:\n"
        "    1 -> #x\n"
        "| prawda =>:\n"
        "    2 -> #x\n"
        "koniec\n");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* query = as<IfStatementNode>(result.program->statements.at(0));
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->trueBranch.size(), 1u);
    ASSERT_EQ(query->falseBranch.size(), 1u);

    const auto* trueAssignment = as<AssignmentNode>(query->trueBranch[0]);
    ASSERT_NE(trueAssignment, nullptr);
    EXPECT_EQ(as<IntegerLiteralNode>(trueAssignment->value)->value, 2);
}

TEST(ParserQuery, NestedQueries) {
    const ParseResult result = parse(
        "?a ?\n"
        "| prawda =>:\n"
        "    ?b ?\n"
        "    | falsz =>:\n"
        "        1 -> #x\n"
        "    koniec\n"
        "koniec\n");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);

    const auto* outer = as<IfStatementNode>(result.program->statements[0]);
    ASSERT_NE(outer, nullptr);
    ASSERT_EQ(outer->trueBranch.size(), 1u);

    const auto* inner = as<IfStatementNode>(outer->trueBranch[0]);
    ASSERT_NE(inner, nullptr);
    EXPECT_TRUE(inner->trueBranch.empty());
    EXPECT_EQ(inner->falseBranch.size(), 1u);
}

TEST(ParserQuery, DuplicateBranchIsAnError) {
    const ParseResult result = parse(
        "?a ?\n"
        "| prawda =>:\n"
        "    1 -> #x\n"
        "| prawda =>:\n"
        "    2 -> #x\n"
        "koniec\n");
    EXPECT_GT(result.errorCount, 0u);
}

TEST(ParserQuery, MissingBranchLabelIsAnError) {
    const ParseResult result = parse(
        "?a ?\n"
        "| dodatnie =>:\n"
        "    1 -> #x\n"
        "koniec\n");
    EXPECT_GT(result.errorCount, 0u);
}

// --- Loops -----------------------------------------------------------------------------

TEST(ParserWhile, LoopWithColon) {
    const ParseResult result = parse(
        "dopoki #z > 0 rob:\n"
        "    #z - 1 -> #z\n"
        "koniec\n");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);

    const auto* loop = as<WhileNode>(result.program->statements[0]);
    ASSERT_NE(loop, nullptr);

    const auto* condition = as<BinaryNode>(loop->condition);
    ASSERT_NE(condition, nullptr);
    EXPECT_EQ(condition->op, BinaryOp::Greater);

    ASSERT_EQ(loop->body.size(), 1u);
    EXPECT_NE(as<AssignmentNode>(loop->body[0]), nullptr);
}

TEST(ParserWhile, ColonAfterRobIsOptional) {
    const ParseResult result = parse("dopoki ?dalej rob 1 -> #x koniec");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);
    EXPECT_NE(as<WhileNode>(result.program->statements[0]), nullptr);
}

TEST(ParserWhile, MissingKoniecIsAnError) {
    const ParseResult result = parse("dopoki prawda rob: 1 -> #x");
    EXPECT_GT(result.errorCount, 0u);
}

// --- Functions ----------------------------------------------------------------------------

TEST(ParserFunction, FullDefinitionWithReturnType) {
    const ParseResult result = parse(
        "powolaj suma(#a, #b) -> #:\n"
        "    #a + #b -> #wynik\n"
        "    <- #wynik\n"
        "koniec\n");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);

    const auto* function = as<FunctionDefNode>(result.program->statements[0]);
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->name, "suma");
    EXPECT_EQ(function->returnType, Type::Int);

    ASSERT_EQ(function->parameters.size(), 2u);
    EXPECT_EQ(function->parameters[0].type, Type::Int);
    EXPECT_EQ(function->parameters[0].name, "a");
    EXPECT_EQ(function->parameters[1].name, "b");

    ASSERT_EQ(function->body.size(), 2u);
    EXPECT_NE(as<AssignmentNode>(function->body[0]), nullptr);

    const auto* returnStatement = as<ReturnNode>(function->body[1]);
    ASSERT_NE(returnStatement, nullptr);
    ASSERT_NE(returnStatement->value, nullptr);
    EXPECT_NE(as<VariableNode>(returnStatement->value), nullptr);
}

TEST(ParserFunction, VoidFunctionWithoutAnnotationAndBareReturn) {
    const ParseResult result = parse(
        "powolaj powitaj($imie)\n"
        "    pisz($imie)\n"
        "    <-\n"
        "koniec\n");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* function = as<FunctionDefNode>(result.program->statements.at(0));
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->returnType, Type::Void);
    ASSERT_EQ(function->parameters.size(), 1u);
    EXPECT_EQ(function->parameters[0].type, Type::String);

    ASSERT_EQ(function->body.size(), 2u);
    const auto* returnStatement = as<ReturnNode>(function->body[1]);
    ASSERT_NE(returnStatement, nullptr);
    EXPECT_EQ(returnStatement->value, nullptr);
}

TEST(ParserFunction, NoParameters) {
    const ParseResult result = parse(
        "powolaj zero() -> #:\n"
        "    <- 0\n"
        "koniec\n");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* function = as<FunctionDefNode>(result.program->statements.at(0));
    ASSERT_NE(function, nullptr);
    EXPECT_TRUE(function->parameters.empty());
    EXPECT_EQ(function->returnType, Type::Int);
}

TEST(ParserFunction, ParameterWithoutSigilIsAnError) {
    const ParseResult result = parse("powolaj zle(a) koniec");
    EXPECT_GT(result.errorCount, 0u);
}

// --- Calls and built-ins ---------------------------------------------------------------------

TEST(ParserCalls, UserCallWithArgumentsAssignsResult) {
    const ParseResult result = parse("suma(#x, 32) -> #z");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* assignment = as<AssignmentNode>(result.program->statements.at(0));
    ASSERT_NE(assignment, nullptr);

    const auto* call = as<CallNode>(assignment->value);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->callee, "suma");
    EXPECT_EQ(call->builtin, BuiltinKind::None);
    ASSERT_EQ(call->arguments.size(), 2u);
    EXPECT_NE(as<VariableNode>(call->arguments[0]), nullptr);
    EXPECT_NE(as<IntegerLiteralNode>(call->arguments[1]), nullptr);
}

TEST(ParserCalls, PiszAndBierzAreMappedToBuiltins) {
    const ParseResult result = parse(
        "pisz(\"podaj liczbe: \")\n"
        "bierz() -> #liczba\n");
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 2u);

    const auto* piszStatement = as<ExpressionStatementNode>(result.program->statements[0]);
    ASSERT_NE(piszStatement, nullptr);
    const auto* piszCall = as<CallNode>(piszStatement->expression);
    ASSERT_NE(piszCall, nullptr);
    EXPECT_EQ(piszCall->builtin, BuiltinKind::Pisz);
    EXPECT_EQ(piszCall->arguments.size(), 1u);

    const auto* bierzAssignment = as<AssignmentNode>(result.program->statements[1]);
    ASSERT_NE(bierzAssignment, nullptr);
    const auto* bierzCall = as<CallNode>(bierzAssignment->value);
    ASSERT_NE(bierzCall, nullptr);
    EXPECT_EQ(bierzCall->builtin, BuiltinKind::Bierz);
    EXPECT_TRUE(bierzCall->arguments.empty());
}

TEST(ParserCalls, OutAndInAreOrdinaryIdentifiers) {
    // The old English names are NOT builtins anymore; they parse as plain
    // calls and are rejected later by the semantic analyzer.
    const ParseResult result = parse("out(\"x\")\nin() -> #a\n");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* outStatement = as<ExpressionStatementNode>(result.program->statements.at(0));
    ASSERT_NE(outStatement, nullptr);
    const auto* outCall = as<CallNode>(outStatement->expression);
    ASSERT_NE(outCall, nullptr);
    EXPECT_EQ(outCall->builtin, BuiltinKind::None);

    const auto* inAssignment = as<AssignmentNode>(result.program->statements.at(1));
    ASSERT_NE(inAssignment, nullptr);
    const auto* inCall = as<CallNode>(inAssignment->value);
    ASSERT_NE(inCall, nullptr);
    EXPECT_EQ(inCall->builtin, BuiltinKind::None);
}

TEST(ParserCalls, NestedCallArguments) {
    const ParseResult result = parse("suma(suma(1, 2), 3 * 4) -> #x");
    EXPECT_EQ(result.errorCount, 0u);

    const auto* assignment = as<AssignmentNode>(result.program->statements.at(0));
    ASSERT_NE(assignment, nullptr);
    const auto* outerCall = as<CallNode>(assignment->value);
    ASSERT_NE(outerCall, nullptr);
    ASSERT_EQ(outerCall->arguments.size(), 2u);
    EXPECT_NE(as<CallNode>(outerCall->arguments[0]), nullptr);
    EXPECT_NE(as<BinaryNode>(outerCall->arguments[1]), nullptr);
}

TEST(ParserCalls, BareIdentifierIsAnError) {
    const ParseResult result = parse("x + 1 -> #y");
    EXPECT_GT(result.errorCount, 0u);
}

// --- Error recovery -----------------------------------------------------------------------------

TEST(ParserRecovery, ReportsMultipleErrorsInOnePass) {
    const ParseResult result = parse(
        "5 -> 6\n"
        "powolaj zle(a) koniec\n"
        "10 -> #ok\n");
    EXPECT_GE(result.errorCount, 2u);

    // The valid trailing statement still parses.
    ASSERT_FALSE(result.program->statements.empty());
    const auto* assignment = as<AssignmentNode>(result.program->statements.back());
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->targetName, "ok");
}

TEST(ParserRecovery, StrayKoniecDoesNotSwallowFollowingStatements) {
    const ParseResult result = parse("koniec 10 -> #x");
    EXPECT_GT(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 1u);
    EXPECT_NE(as<AssignmentNode>(result.program->statements[0]), nullptr);
}

// --- Whole-program smoke test ----------------------------------------------------------------------

TEST(ParserProgram, FullNurtProgram) {
    constexpr std::string_view program =
        "# hello.nrt\n"
        "powolaj suma(#a, #b) -> #:\n"
        "    #a + #b -> #wynik\n"
        "    <- #wynik\n"
        "koniec\n"
        "\n"
        "10 -> #x\n"
        "32 -> #y\n"
        "suma(#x, #y) -> #z\n"
        "\n"
        "dopoki #z > 0 rob:\n"
        "    #z - 1 -> #z\n"
        "koniec\n"
        "\n"
        "prawda -> ?gotowe\n"
        "\"Witaj, Nurt!\\n\" -> $powitanie\n"
        "\n"
        "?gotowe ?\n"
        "| prawda =>:\n"
        "    pisz($powitanie)\n"
        "| falsz =>:\n"
        "    pisz(\"jeszcze nie...\\n\")\n"
        "koniec\n";

    const ParseResult result = parse(program);
    EXPECT_EQ(result.errorCount, 0u);
    ASSERT_EQ(result.program->statements.size(), 8u);

    EXPECT_NE(as<FunctionDefNode>(result.program->statements[0]), nullptr);
    EXPECT_NE(as<AssignmentNode>(result.program->statements[1]), nullptr);
    EXPECT_NE(as<AssignmentNode>(result.program->statements[2]), nullptr);
    EXPECT_NE(as<AssignmentNode>(result.program->statements[3]), nullptr);
    EXPECT_NE(as<WhileNode>(result.program->statements[4]), nullptr);
    EXPECT_NE(as<AssignmentNode>(result.program->statements[5]), nullptr);
    EXPECT_NE(as<AssignmentNode>(result.program->statements[6]), nullptr);

    const auto* query = as<IfStatementNode>(result.program->statements[7]);
    ASSERT_NE(query, nullptr);
    EXPECT_EQ(query->trueBranch.size(), 1u);
    EXPECT_EQ(query->falseBranch.size(), 1u);
}
