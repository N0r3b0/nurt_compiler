// Dedicated suite for the Step 5 upgrades: the 'dopasuj' match statement and
// the keyword logical operators 'i' / 'lub'.

#include "codegen/codegen.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "semantic/analyzer.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using nurt::AssignmentNode;
using nurt::BinaryNode;
using nurt::BinaryOp;
using nurt::BoolLiteralNode;
using nurt::IntegerLiteralNode;
using nurt::MatchStatementNode;
using nurt::StringLiteralNode;
using nurt::Type;
using nurt::VariableNode;

struct PipelineResult {
    std::unique_ptr<nurt::ProgramNode> program;
    std::size_t parseErrors = 0;     ///< lexer + parser errors
    std::size_t semanticErrors = 0;  ///< errors added by the analyzer
    std::string assembly;            ///< filled only when the front end was clean
    std::vector<nurt::Diagnostic> diagnostics;
};

/// Runs the full front end (and codegen when clean). NOTE: pass string
/// literals - diagnostics view into the buffer.
PipelineResult run(std::string_view source) {
    nurt::DiagnosticEngine diagnostics("test.nrt", source);
    nurt::Lexer lexer(source, diagnostics);
    nurt::Parser parser(lexer.tokenize(), diagnostics);

    PipelineResult result;
    result.program = parser.parseProgram();
    result.parseErrors = diagnostics.errorCount();

    nurt::SemanticAnalyzer analyzer(diagnostics);
    analyzer.analyze(*result.program);
    result.semanticErrors = diagnostics.errorCount() - result.parseErrors;

    if (diagnostics.errorCount() == 0) {
        nurt::CodeGenerator codegen(diagnostics);
        result.assembly = codegen.generate(*result.program);
    }
    result.diagnostics = diagnostics.diagnostics();
    return result;
}

bool hasErrorContaining(const PipelineResult& result, std::string_view fragment) {
    for (const nurt::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == nurt::Severity::Error &&
            diagnostic.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

template <typename T, typename Ptr>
const T* as(const Ptr& node) {
    return dynamic_cast<const T*>(node.get());
}

} // namespace

// --- 'i' / 'lub' keyword operators ----------------------------------------------

TEST(LogicalKeywords, ParseWithCorrectPrecedenceAndType) {
    const PipelineResult result = run(
        "prawda -> ?a\n"
        "falsz -> ?b\n"
        "?a i ?b lub !?a -> ?w\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 0u);

    // (?a i ?b) lub (!?a) - 'i' binds tighter than 'lub'.
    const auto* assignment = as<AssignmentNode>(result.program->statements.at(2));
    ASSERT_NE(assignment, nullptr);
    const auto* orNode = as<BinaryNode>(assignment->value);
    ASSERT_NE(orNode, nullptr);
    EXPECT_EQ(orNode->op, BinaryOp::Or);
    const auto* andNode = as<BinaryNode>(orNode->lhs);
    ASSERT_NE(andNode, nullptr);
    EXPECT_EQ(andNode->op, BinaryOp::And);
}

TEST(LogicalKeywords, OldSymbolsAreCompileErrors) {
    const PipelineResult andResult = run("prawda && falsz -> ?w");
    EXPECT_GT(andResult.parseErrors, 0u);
    EXPECT_TRUE(hasErrorContaining(andResult, "keyword 'i'"));

    const PipelineResult orResult = run("prawda || falsz -> ?w");
    EXPECT_GT(orResult.parseErrors, 0u);
    EXPECT_TRUE(hasErrorContaining(orResult, "keyword 'lub'"));
}

TEST(LogicalKeywords, IAndLubAreReservedWords) {
    // 'i' can no longer be a variable name; the sigil reference '#i' fails.
    const PipelineResult result = run("5 -> #i");
    EXPECT_GT(result.parseErrors, 0u);
}

TEST(LogicalKeywords, LowerToBitwiseInstructions) {
    const PipelineResult result = run(
        "prawda -> ?a\n"
        "?a i ?a -> ?b\n"
        "?a lub ?b -> ?c\n");
    EXPECT_EQ(result.semanticErrors, 0u);
    EXPECT_NE(result.assembly.find("and rax, rbx"), std::string::npos);
    EXPECT_NE(result.assembly.find("or rax, rbx"), std::string::npos);
}

// --- 'dopasuj' parsing -------------------------------------------------------------

TEST(DopasujParser, IntCasesWithInaczej) {
    const PipelineResult result = run(
        "5 -> #x\n"
        "dopasuj #x\n"
        "| 1 =>:\n"
        "    pisz(\"jeden\")\n"
        "| 2 =>:\n"
        "    pisz(\"dwa\")\n"
        "| inaczej =>:\n"
        "    pisz(\"inne\")\n"
        "koniec\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 0u);

    const auto* match = as<MatchStatementNode>(result.program->statements.at(1));
    ASSERT_NE(match, nullptr);

    const auto* target = as<VariableNode>(match->target);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->type, Type::Int);

    ASSERT_EQ(match->cases.size(), 2u);
    const auto* first = as<IntegerLiteralNode>(match->cases[0].literal);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->value, 1);
    EXPECT_EQ(match->cases[0].body.size(), 1u);
    EXPECT_EQ(match->defaultBranch.size(), 1u);
}

TEST(DopasujParser, NegativeIntegerCase) {
    const PipelineResult result = run(
        "0 -> #x\n"
        "dopasuj #x\n"
        "| -5 =>:\n"
        "    pisz(\"minus piec\")\n"
        "| inaczej =>:\n"
        "    pisz(\"inne\")\n"
        "koniec\n");
    EXPECT_EQ(result.parseErrors, 0u);

    const auto* match = as<MatchStatementNode>(result.program->statements.at(1));
    ASSERT_NE(match, nullptr);
    const auto* literal = as<IntegerLiteralNode>(match->cases.at(0).literal);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->value, -5);
}

TEST(DopasujParser, StringAndBoolCases) {
    const PipelineResult result = run(
        "\"plus\" -> $op\n"
        "dopasuj $op\n"
        "| \"plus\" =>:\n"
        "    pisz(\"+\")\n"
        "| inaczej =>:\n"
        "    pisz(\"?\")\n"
        "koniec\n"
        "prawda -> ?f\n"
        "dopasuj ?f\n"
        "| prawda =>:\n"
        "    pisz(\"tak\")\n"
        "| falsz =>:\n"
        "    pisz(\"nie\")\n"
        "| inaczej =>:\n"
        "    pisz(\"?\")\n"
        "koniec\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 0u);

    const auto* stringMatch = as<MatchStatementNode>(result.program->statements.at(1));
    ASSERT_NE(stringMatch, nullptr);
    EXPECT_NE(as<StringLiteralNode>(stringMatch->cases.at(0).literal), nullptr);

    const auto* boolMatch = as<MatchStatementNode>(result.program->statements.at(3));
    ASSERT_NE(boolMatch, nullptr);
    ASSERT_EQ(boolMatch->cases.size(), 2u);
    EXPECT_NE(as<BoolLiteralNode>(boolMatch->cases.at(1).literal), nullptr);
}

TEST(DopasujParser, MatchOnExpressionTarget) {
    const PipelineResult result = run(
        "7 -> #x\n"
        "dopasuj #x % 2\n"
        "| 0 =>:\n"
        "    pisz(\"parzyste\")\n"
        "| inaczej =>:\n"
        "    pisz(\"nieparzyste\")\n"
        "koniec\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(DopasujParser, MissingInaczejIsAnError) {
    const PipelineResult result = run(
        "1 -> #x\n"
        "dopasuj #x\n"
        "| 1 =>:\n"
        "    pisz(\"jeden\")\n"
        "koniec\n");
    EXPECT_GT(result.parseErrors, 0u);
    EXPECT_TRUE(hasErrorContaining(result, "requires a final '| inaczej =>:' branch"));
}

TEST(DopasujParser, InaczejMustBeLast) {
    const PipelineResult result = run(
        "1 -> #x\n"
        "dopasuj #x\n"
        "| inaczej =>:\n"
        "    pisz(\"inne\")\n"
        "| 1 =>:\n"
        "    pisz(\"jeden\")\n"
        "koniec\n");
    EXPECT_GT(result.parseErrors, 0u);
    EXPECT_TRUE(hasErrorContaining(result, "'inaczej' must be the last branch"));
}

TEST(DopasujParser, DuplicateInaczejIsAnError) {
    const PipelineResult result = run(
        "1 -> #x\n"
        "dopasuj #x\n"
        "| inaczej =>:\n"
        "    pisz(\"a\")\n"
        "| inaczej =>:\n"
        "    pisz(\"b\")\n"
        "koniec\n");
    EXPECT_GT(result.parseErrors, 0u);
    EXPECT_TRUE(hasErrorContaining(result, "duplicate 'inaczej' branch"));
}

TEST(DopasujParser, NonLiteralCaseIsAnError) {
    const PipelineResult result = run(
        "1 -> #x\n"
        "1 -> #y\n"
        "dopasuj #x\n"
        "| #y =>:\n"
        "    pisz(\"zmienna\")\n"
        "| inaczej =>:\n"
        "    pisz(\"inne\")\n"
        "koniec\n");
    EXPECT_GT(result.parseErrors, 0u);
    EXPECT_TRUE(hasErrorContaining(result, "expected a literal"));
}

TEST(DopasujParser, NestedDopasujAndQuery) {
    const PipelineResult result = run(
        "1 -> #x\n"
        "prawda -> ?f\n"
        "dopasuj #x\n"
        "| 1 =>:\n"
        "    ?f ?\n"
        "    | prawda =>:\n"
        "        dopasuj #x + 1\n"
        "        | 2 =>:\n"
        "            pisz(\"dwa\")\n"
        "        | inaczej =>:\n"
        "            pisz(\"inne\")\n"
        "        koniec\n"
        "    koniec\n"
        "| inaczej =>:\n"
        "    pisz(\"inne\")\n"
        "koniec\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 0u);
}

// --- 'dopasuj' semantic checks ----------------------------------------------------------

TEST(DopasujSemantics, CaseLiteralMustMatchTargetType) {
    const PipelineResult result = run(
        "1 -> #x\n"
        "dopasuj #x\n"
        "| \"jeden\" =>:\n"
        "    pisz(\"napis\")\n"
        "| inaczej =>:\n"
        "    pisz(\"inne\")\n"
        "koniec\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "case type mismatch"));
}

TEST(DopasujSemantics, DuplicateIntCaseIsAnError) {
    const PipelineResult result = run(
        "1 -> #x\n"
        "dopasuj #x\n"
        "| 7 =>:\n"
        "    pisz(\"a\")\n"
        "| 7 =>:\n"
        "    pisz(\"b\")\n"
        "| inaczej =>:\n"
        "    pisz(\"c\")\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "duplicate 'dopasuj' case '7'"));
}

TEST(DopasujSemantics, DuplicateStringAndBoolCasesAreErrors) {
    const PipelineResult strings = run(
        "\"a\" -> $s\n"
        "dopasuj $s\n"
        "| \"x\" =>:\n"
        "    pisz(\"1\")\n"
        "| \"x\" =>:\n"
        "    pisz(\"2\")\n"
        "| inaczej =>:\n"
        "    pisz(\"3\")\n"
        "koniec\n");
    EXPECT_EQ(strings.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(strings, "duplicate 'dopasuj' case '\"x\"'"));

    const PipelineResult bools = run(
        "prawda -> ?f\n"
        "dopasuj ?f\n"
        "| prawda =>:\n"
        "    pisz(\"1\")\n"
        "| prawda =>:\n"
        "    pisz(\"2\")\n"
        "| inaczej =>:\n"
        "    pisz(\"3\")\n"
        "koniec\n");
    EXPECT_EQ(bools.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(bools, "duplicate 'dopasuj' case 'prawda'"));
}

TEST(DopasujSemantics, TargetMustBeAValue) {
    const PipelineResult result = run(
        "powolaj nic() koniec\n"
        "dopasuj nic()\n"
        "| inaczej =>:\n"
        "    pisz(\"x\")\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "has no value"));
}

TEST(DopasujSemantics, CountsTowardReturnPathCoverage) {
    const PipelineResult covered = run(
        "powolaj nazwij(#n) -> $:\n"
        "    dopasuj #n\n"
        "    | 1 =>:\n"
        "        <- \"jeden\"\n"
        "    | 2 =>:\n"
        "        <- \"dwa\"\n"
        "    | inaczej =>:\n"
        "        <- \"inne\"\n"
        "    koniec\n"
        "koniec\n");
    EXPECT_EQ(covered.parseErrors, 0u);
    EXPECT_EQ(covered.semanticErrors, 0u);

    const PipelineResult uncovered = run(
        "powolaj nazwij(#n) -> $:\n"
        "    dopasuj #n\n"
        "    | 1 =>:\n"
        "        <- \"jeden\"\n"
        "    | 2 =>:\n"
        "        pisz(\"brak zwrotu\")\n"
        "    | inaczej =>:\n"
        "        <- \"inne\"\n"
        "    koniec\n"
        "koniec\n");
    EXPECT_EQ(uncovered.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(uncovered, "every control path"));
}

// --- 'dopasuj' code generation ------------------------------------------------------------

TEST(DopasujCodegen, IntMatchEmitsCmpJeChain) {
    const PipelineResult result = run(
        "5 -> #x\n"
        "dopasuj #x\n"
        "| 1 =>:\n"
        "    pisz(\"jeden\")\n"
        "| 42 =>:\n"
        "    pisz(\"odpowiedz\")\n"
        "| inaczej =>:\n"
        "    pisz(\"inne\")\n"
        "koniec\n");
    ASSERT_EQ(result.parseErrors + result.semanticErrors, 0u);

    EXPECT_NE(result.assembly.find("cmp rax, 1"), std::string::npos);
    EXPECT_NE(result.assembly.find("cmp rax, 42"), std::string::npos);
    EXPECT_NE(result.assembly.find("je .przypadek_"), std::string::npos);
    EXPECT_NE(result.assembly.find("jmp .inaczej_"), std::string::npos);
    EXPECT_NE(result.assembly.find(".koniec_dopasuj_"), std::string::npos);
}

TEST(DopasujCodegen, Int64CaseGoesThroughRegister) {
    const PipelineResult result = run(
        "5 -> #x\n"
        "dopasuj #x\n"
        "| 5000000000 =>:\n"
        "    pisz(\"duze\")\n"
        "| inaczej =>:\n"
        "    pisz(\"inne\")\n"
        "koniec\n");
    ASSERT_EQ(result.parseErrors + result.semanticErrors, 0u);

    // 5e9 does not fit a sign-extended imm32, so it must be staged in rcx.
    EXPECT_NE(result.assembly.find("mov rcx, 5000000000"), std::string::npos);
    EXPECT_NE(result.assembly.find("cmp rax, rcx"), std::string::npos);
}

TEST(DopasujCodegen, StringMatchComparesContentViaStrcmp) {
    const PipelineResult result = run(
        "\"plus\" -> $op\n"
        "dopasuj $op\n"
        "| \"plus\" =>:\n"
        "    pisz(\"+\")\n"
        "| \"minus\" =>:\n"
        "    pisz(\"-\")\n"
        "| inaczej =>:\n"
        "    pisz(\"?\")\n"
        "koniec\n");
    ASSERT_EQ(result.parseErrors + result.semanticErrors, 0u);

    EXPECT_NE(result.assembly.find("call strcmp"), std::string::npos);
    EXPECT_NE(result.assembly.find("mov rdi, [rsp]"), std::string::npos);
    EXPECT_NE(result.assembly.find("add rsp, 8"), std::string::npos);
}
