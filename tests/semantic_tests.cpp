#include "semantic/analyzer.hpp"

#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

struct AnalysisResult {
    std::size_t parseErrors = 0;     ///< lexer + parser errors (expected 0 in these tests)
    std::size_t semanticErrors = 0;  ///< errors added by the semantic analyzer
    std::size_t warnings = 0;
    std::vector<nurt::Diagnostic> diagnostics;
};

/// Lexes, parses, and analyzes `source`. NOTE: pass string literals (or
/// otherwise keep the buffer alive) - diagnostics view into it.
AnalysisResult analyze(std::string_view source) {
    nurt::DiagnosticEngine diagnostics("test.nrt", source);
    nurt::Lexer lexer(source, diagnostics);
    nurt::Parser parser(lexer.tokenize(), diagnostics);
    const auto program = parser.parseProgram();

    AnalysisResult result;
    result.parseErrors = diagnostics.errorCount();

    nurt::SemanticAnalyzer analyzer(diagnostics);
    analyzer.analyze(*program);

    result.semanticErrors = diagnostics.errorCount() - result.parseErrors;
    result.diagnostics = diagnostics.diagnostics();
    for (const nurt::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == nurt::Severity::Warning) {
            ++result.warnings;
        }
    }
    return result;
}

/// True when some error diagnostic contains `fragment`.
bool hasErrorContaining(const AnalysisResult& result, std::string_view fragment) {
    for (const nurt::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == nurt::Severity::Error &&
            diagnostic.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

// --- Scope and symbol validation ------------------------------------------------

TEST(SemanticScopes, AssignmentDeclaresVariableForLaterUse) {
    const AnalysisResult result = analyze(
        "10 -> #x\n"
        "#x + 1 -> #y\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(SemanticScopes, UseBeforeAssignmentIsAnError) {
    const AnalysisResult result = analyze("#x + 1 -> #y");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "used before a value flows into it"));
}

TEST(SemanticScopes, SigilMismatchOnUseIsAnError) {
    const AnalysisResult result = analyze(
        "10 -> #x\n"
        "pisz($x)\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "cannot be referenced as '$x'"));
}

TEST(SemanticScopes, SigilMismatchOnReassignmentIsAnError) {
    const AnalysisResult result = analyze(
        "10 -> #x\n"
        "\"tekst\" -> $x\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "cannot be reassigned as '$x'"));
}

TEST(SemanticScopes, ReassignmentWithSameSigilIsFine) {
    const AnalysisResult result = analyze(
        "10 -> #x\n"
        "#x + 1 -> #x\n");
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(SemanticScopes, GlobalsAreNotVisibleInsideFunctions) {
    const AnalysisResult result = analyze(
        "10 -> #x\n"
        "powolaj f() -> #:\n"
        "    <- #x\n"
        "koniec\n");
    EXPECT_GE(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "used before a value flows into it"));
}

TEST(SemanticScopes, ParametersAreVisibleInsideTheFunction) {
    const AnalysisResult result = analyze(
        "powolaj podwoj(#n) -> #:\n"
        "    <- #n * 2\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(SemanticScopes, LocalsDoNotLeakOutOfTheFunction) {
    const AnalysisResult result = analyze(
        "powolaj f()\n"
        "    10 -> #lokalna\n"
        "koniec\n"
        "#lokalna -> #x\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'#lokalna' is used before"));
}

TEST(SemanticScopes, FunctionMustBeDefinedBeforeCall) {
    const AnalysisResult result = analyze(
        "suma(1, 2) -> #x\n"
        "powolaj suma(#a, #b) -> #:\n"
        "    <- #a + #b\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "call to undefined function 'suma'"));
}

TEST(SemanticScopes, DirectRecursionIsAllowed) {
    const AnalysisResult result = analyze(
        "powolaj silnia(#n) -> #:\n"
        "    #n <= 1 ?\n"
        "    | prawda =>:\n"
        "        <- 1\n"
        "    | falsz =>:\n"
        "        <- #n * silnia(#n - 1)\n"
        "    koniec\n"
        "koniec\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(SemanticScopes, DuplicateFunctionIsAnError) {
    const AnalysisResult result = analyze(
        "powolaj f() koniec\n"
        "powolaj f() koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'f' is already defined"));
}

TEST(SemanticScopes, DuplicateParameterIsAnError) {
    const AnalysisResult result = analyze("powolaj f(#a, #a) koniec");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "duplicate parameter name 'a'"));
}

TEST(SemanticScopes, NestedFunctionDefinitionIsAnError) {
    const AnalysisResult result = analyze(
        "powolaj zewnetrzna()\n"
        "    powolaj wewnetrzna() koniec\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "only allowed at the top level"));
}

TEST(SemanticScopes, RedefiningBuiltinIsAnError) {
    const AnalysisResult result = analyze("powolaj pisz($t) koniec");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "cannot redefine the built-in function 'pisz'"));
}

// --- Assignment type checking ---------------------------------------------------------

TEST(SemanticAssignment, ExpressionMustMatchTargetSigil) {
    const AnalysisResult result = analyze("\"tekst\" -> #liczba");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "cannot flow into '#liczba'"));
}

TEST(SemanticAssignment, ComparisonResultFlowsIntoBool) {
    const AnalysisResult result = analyze(
        "5 -> #x\n"
        "#x > 3 -> ?duze\n");
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(SemanticAssignment, ComparisonResultCannotFlowIntoInt) {
    const AnalysisResult result = analyze(
        "5 -> #x\n"
        "#x > 3 -> #zle\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "bool (?) value cannot flow into '#zle'"));
}

TEST(SemanticAssignment, VoidCallCannotBeAssigned) {
    const AnalysisResult result = analyze(
        "powolaj nic() koniec\n"
        "nic() -> #x\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "has no value"));
}

// --- Operator type checking ----------------------------------------------------------------

TEST(SemanticOperators, CannotAddStringToInteger) {
    const AnalysisResult result = analyze(
        "\"abc\" -> $s\n"
        "$s + 1 -> #x\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'+' requires int (#) operands"));
}

TEST(SemanticOperators, ArithmeticOnIntsIsFine) {
    const AnalysisResult result = analyze("1 + 2 * 3 % 4 - 5 / 2 -> #x");
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(SemanticOperators, OrderingRequiresInts) {
    const AnalysisResult result = analyze(
        "\"a\" -> $a\n"
        "\"b\" -> $b\n"
        "$a < $b -> ?w\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'<' requires int (#) operands"));
}

TEST(SemanticOperators, EqualityRequiresMatchingTypes) {
    const AnalysisResult mixed = analyze("1 == prawda -> ?w");
    EXPECT_EQ(mixed.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(mixed, "'==' requires operands of the same type"));

    const AnalysisResult strings = analyze(
        "\"a\" -> $a\n"
        "$a == \"a\" -> ?w\n");
    EXPECT_EQ(strings.semanticErrors, 0u);
}

TEST(SemanticOperators, LogicalOperatorsRequireBools) {
    const AnalysisResult result = analyze("1 && 2 -> ?w");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'&&' requires bool (?) operands"));
}

TEST(SemanticOperators, UnaryOperators) {
    const AnalysisResult negateString = analyze(
        "\"a\" -> $a\n"
        "-$a -> #x\n");
    EXPECT_EQ(negateString.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(negateString, "unary '-' requires an int (#) operand"));

    const AnalysisResult notInt = analyze("!5 -> ?w");
    EXPECT_EQ(notInt.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(notInt, "'!' requires a bool (?) operand"));

    const AnalysisResult fine = analyze(
        "prawda -> ?p\n"
        "!?p -> ?q\n"
        "-5 -> #m\n");
    EXPECT_EQ(fine.semanticErrors, 0u);
}

// --- Conditions ---------------------------------------------------------------------------------

TEST(SemanticConditions, QueryConditionMustBeBool) {
    const AnalysisResult result = analyze(
        "5 -> #x\n"
        "#x ?\n"
        "| prawda =>:\n"
        "    pisz(\"tak\")\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'?' query condition must be a bool"));
}

TEST(SemanticConditions, BoolExpressionConditionIsFine) {
    const AnalysisResult result = analyze(
        "5 -> #x\n"
        "#x > 3 ?\n"
        "| prawda =>:\n"
        "    pisz(\"tak\")\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(SemanticConditions, DopokiConditionMustBeBool) {
    const AnalysisResult result = analyze(
        "\"tekst\" -> $t\n"
        "dopoki $t rob:\n"
        "    pisz($t)\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'dopoki' loop condition must be a bool"));
}

// --- Returns ----------------------------------------------------------------------------------------

TEST(SemanticReturns, ReturnTypeMustMatchDeclaredSigil) {
    const AnalysisResult result = analyze(
        "powolaj f() -> #:\n"
        "    <- \"tekst\"\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "return type mismatch"));
}

TEST(SemanticReturns, BareReturnInNonVoidFunctionIsAnError) {
    const AnalysisResult result = analyze(
        "powolaj f() -> #:\n"
        "    <-\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "must return int (#)"));
}

TEST(SemanticReturns, ValueReturnInVoidFunctionIsAnError) {
    const AnalysisResult result = analyze(
        "powolaj f()\n"
        "    <- 5\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "void function 'f' cannot return a value"));
}

TEST(SemanticReturns, ReturnOutsideFunctionIsAnError) {
    const AnalysisResult result = analyze("<- 5");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "only allowed inside a 'powolaj' function"));
}

TEST(SemanticReturns, NonVoidFunctionMustReturnOnEveryPath) {
    const AnalysisResult missing = analyze(
        "powolaj f(?w) -> #:\n"
        "    ?w ?\n"
        "    | prawda =>:\n"
        "        <- 1\n"
        "    koniec\n"
        "koniec\n");
    EXPECT_EQ(missing.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(missing, "every control path"));

    const AnalysisResult covered = analyze(
        "powolaj f(?w) -> #:\n"
        "    ?w ?\n"
        "    | prawda =>:\n"
        "        <- 1\n"
        "    | falsz =>:\n"
        "        <- 2\n"
        "    koniec\n"
        "koniec\n");
    EXPECT_EQ(covered.semanticErrors, 0u);
}

TEST(SemanticReturns, LoopDoesNotGuaranteeReturn) {
    const AnalysisResult result = analyze(
        "powolaj f(?w) -> #:\n"
        "    dopoki ?w rob:\n"
        "        <- 1\n"
        "    koniec\n"
        "koniec\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "every control path"));
}

// --- Calls and built-ins ---------------------------------------------------------------------------------

TEST(SemanticCalls, ArgumentCountMustMatch) {
    const AnalysisResult result = analyze(
        "powolaj suma(#a, #b) -> #:\n"
        "    <- #a + #b\n"
        "koniec\n"
        "suma(1) -> #x\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "expects 2 argument(s), 1 given"));
}

TEST(SemanticCalls, ArgumentTypesMustMatchParameterSigils) {
    const AnalysisResult result = analyze(
        "powolaj suma(#a, #b) -> #:\n"
        "    <- #a + #b\n"
        "koniec\n"
        "suma(1, \"dwa\") -> #x\n");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "argument 2 of 'suma' must be int (#)"));
}

TEST(SemanticCalls, IgnoredResultProducesWarningNotError) {
    const AnalysisResult result = analyze(
        "powolaj suma(#a, #b) -> #:\n"
        "    <- #a + #b\n"
        "koniec\n"
        "suma(1, 2)\n");
    EXPECT_EQ(result.semanticErrors, 0u);
    EXPECT_EQ(result.warnings, 1u);
}

TEST(SemanticBuiltins, PiszAcceptsAnyValueTypes) {
    const AnalysisResult result = analyze(
        "42 -> #x\n"
        "prawda -> ?p\n"
        "pisz(\"x = \", #x, \"; p = \", ?p, \"\\n\")\n");
    EXPECT_EQ(result.semanticErrors, 0u);
    EXPECT_EQ(result.warnings, 0u);
}

TEST(SemanticBuiltins, PiszRequiresAtLeastOneArgument) {
    const AnalysisResult result = analyze("pisz()");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'pisz' requires at least one argument"));
}

TEST(SemanticBuiltins, BierzTakesItsTypeFromTheTarget) {
    const AnalysisResult result = analyze(
        "bierz() -> #liczba\n"
        "bierz() -> $linia\n");
    EXPECT_EQ(result.semanticErrors, 0u);
}

TEST(SemanticBuiltins, BierzCannotReadIntoBool) {
    const AnalysisResult result = analyze("bierz() -> ?flaga");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "not a bool"));
}

TEST(SemanticBuiltins, BierzTakesNoArguments) {
    const AnalysisResult result = analyze("bierz(5) -> #x");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "'bierz' takes no arguments"));
}

TEST(SemanticBuiltins, BierzMustFlowDirectlyIntoAVariable) {
    const AnalysisResult result = analyze("bierz() + 1 -> #x");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "must flow directly into a variable"));
}

TEST(SemanticBuiltins, OldEnglishNamesGetAHint) {
    const AnalysisResult result = analyze("out(\"x\")");
    EXPECT_EQ(result.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(result, "did you mean the built-in 'pisz'?"));

    const AnalysisResult input = analyze("in() -> #x");
    EXPECT_EQ(input.semanticErrors, 1u);
    EXPECT_TRUE(hasErrorContaining(input, "did you mean the built-in 'bierz'?"));
}

// --- Whole-program smoke test --------------------------------------------------------------------------------

TEST(SemanticProgram, FullValidProgramHasNoErrors) {
    const AnalysisResult result = analyze(
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
        "koniec\n");
    EXPECT_EQ(result.parseErrors, 0u);
    EXPECT_EQ(result.semanticErrors, 0u);
    EXPECT_EQ(result.warnings, 0u);
}

TEST(SemanticProgram, MultipleErrorsAreAllReported) {
    const AnalysisResult result = analyze(
        "\"tekst\" -> #x\n"
        "#brak + 1 -> #y\n"
        "1 && 2 -> ?w\n");
    EXPECT_EQ(result.semanticErrors, 3u);
}
