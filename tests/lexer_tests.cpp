#include "lexer/lexer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using nurt::Token;
using nurt::TokenType;

struct LexResult {
    std::vector<Token> tokens;
    std::vector<nurt::Diagnostic> diagnostics;
    std::size_t errorCount = 0;
};

/// Lexes `source` to completion. NOTE: token lexemes view into `source`, so
/// callers must pass string literals or otherwise keep the buffer alive.
LexResult lex(std::string_view source) {
    nurt::DiagnosticEngine diagnostics("test.nrt", source);
    nurt::Lexer lexer(source, diagnostics);
    LexResult result;
    result.tokens = lexer.tokenize();
    result.diagnostics = diagnostics.diagnostics();
    result.errorCount = diagnostics.errorCount();
    return result;
}

std::vector<TokenType> typesOf(const std::vector<Token>& tokens) {
    std::vector<TokenType> types;
    types.reserve(tokens.size());
    for (const Token& token : tokens) {
        types.push_back(token.type);
    }
    return types;
}

} // namespace

// --- Keywords ----------------------------------------------------------------

TEST(LexerKeywords, AllPolishKeywordsAreRecognized) {
    const LexResult result = lex("powolaj koniec inaczej dopoki rob prawda falsz");
    EXPECT_EQ(result.errorCount, 0u);
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{
                  TokenType::KwPowolaj, TokenType::KwKoniec, TokenType::KwInaczej,
                  TokenType::KwDopoki, TokenType::KwRob, TokenType::KwPrawda, TokenType::KwFalsz,
                  TokenType::EndOfFile}));
}

TEST(LexerKeywords, KeywordsAreCaseSensitive) {
    const LexResult result = lex("Powolaj KONIEC");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Identifier, TokenType::Identifier,
                                      TokenType::EndOfFile}));
}

TEST(LexerKeywords, KeywordPrefixIsAnIdentifier) {
    const LexResult result = lex("powolajmy konieczny robak");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Identifier, TokenType::Identifier,
                                      TokenType::Identifier, TokenType::EndOfFile}));
}

// --- Sigils and the '#' disambiguation rule -----------------------------------

TEST(LexerSigils, HashFollowedByIdentifierIsSigil) {
    const LexResult result = lex("#x $tekst ?flaga");
    ASSERT_EQ(result.tokens.size(), 7u);
    EXPECT_EQ(result.tokens[0].type, TokenType::HashSigil);
    EXPECT_EQ(result.tokens[1].type, TokenType::Identifier);
    EXPECT_EQ(result.tokens[1].lexeme, "x");
    EXPECT_EQ(result.tokens[2].type, TokenType::DollarSigil);
    EXPECT_EQ(result.tokens[3].lexeme, "tekst");
    EXPECT_EQ(result.tokens[4].type, TokenType::QuestionSigil);
    EXPECT_EQ(result.tokens[5].lexeme, "flaga");
    EXPECT_EQ(result.errorCount, 0u);
}

TEST(LexerSigils, HashUnderscoreIsSigil) {
    const LexResult result = lex("#_tmp");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::HashSigil, TokenType::Identifier,
                                      TokenType::EndOfFile}));
}

TEST(LexerComments, HashSpaceStartsComment) {
    const LexResult result = lex("# this whole line vanishes\n42");
    ASSERT_EQ(result.tokens.size(), 2u);
    EXPECT_EQ(result.tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(result.tokens[0].intValue, 42);
}

TEST(LexerComments, HashAtEndOfLineIsComment) {
    const LexResult result = lex("1 #\n2");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::IntegerLiteral, TokenType::IntegerLiteral,
                                      TokenType::EndOfFile}));
}

TEST(LexerComments, HashDigitIsComment) {
    // Per the disambiguation rule, '#' is a sigil only before [A-Za-z_].
    const LexResult result = lex("#1 these are not tokens\nprawda");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::KwPrawda, TokenType::EndOfFile}));
}

TEST(LexerComments, TrailingCommentAfterCode) {
    const LexResult result = lex("10 -> #x # store ten into x");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::IntegerLiteral, TokenType::Arrow,
                                      TokenType::HashSigil, TokenType::Identifier,
                                      TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 0u);
}

TEST(LexerComments, CommentAtEndOfFileWithoutNewline) {
    const LexResult result = lex("falsz # no trailing newline");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::KwFalsz, TokenType::EndOfFile}));
}

// --- Arrows and comparison operators -------------------------------------------

TEST(LexerArrows, ArrowAssignment) {
    const LexResult result = lex("10 -> #x");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::IntegerLiteral, TokenType::Arrow,
                                      TokenType::HashSigil, TokenType::Identifier,
                                      TokenType::EndOfFile}));
}

TEST(LexerArrows, ReturnArrow) {
    const LexResult result = lex("<- #wynik");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::ReturnArrow, TokenType::HashSigil,
                                      TokenType::Identifier, TokenType::EndOfFile}));
}

TEST(LexerArrows, ArrowVersusMinusAndComparisons) {
    const LexResult result = lex("- -> < <- <= > >= == !=");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Minus, TokenType::Arrow, TokenType::Less,
                                      TokenType::ReturnArrow, TokenType::LessEqual,
                                      TokenType::Greater, TokenType::GreaterEqual,
                                      TokenType::EqualEqual, TokenType::BangEqual,
                                      TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 0u);
}

TEST(LexerArrows, LessThanNegativeNeedsSpace) {
    // 'a < -b' (with space) is Less, Minus; 'a <-b' lexes as ReturnArrow.
    const LexResult spaced = lex("#a < -#b");
    EXPECT_EQ(typesOf(spaced.tokens),
              (std::vector<TokenType>{TokenType::HashSigil, TokenType::Identifier, TokenType::Less,
                                      TokenType::Minus, TokenType::HashSigil,
                                      TokenType::Identifier, TokenType::EndOfFile}));

    const LexResult glued = lex("#a <-#b");
    EXPECT_EQ(typesOf(glued.tokens),
              (std::vector<TokenType>{TokenType::HashSigil, TokenType::Identifier,
                                      TokenType::ReturnArrow, TokenType::HashSigil,
                                      TokenType::Identifier, TokenType::EndOfFile}));
}

// --- Operators and punctuation ---------------------------------------------------

TEST(LexerOperators, ArithmeticLogicalAndPunctuation) {
    const LexResult result = lex("+ - * / % ! && || ( ) ,");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Plus, TokenType::Minus, TokenType::Star,
                                      TokenType::Slash, TokenType::Percent, TokenType::Bang,
                                      TokenType::AmpAmp, TokenType::PipePipe, TokenType::LParen,
                                      TokenType::RParen, TokenType::Comma, TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 0u);
}

TEST(LexerOperators, StrayCharactersProduceErrors) {
    const LexResult result = lex("= & @");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Unknown, TokenType::Unknown, TokenType::Unknown,
                                      TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 3u);
}

TEST(LexerOperators, QueryAndBlockTokens) {
    const LexResult result = lex("| => : ||");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Pipe, TokenType::FatArrow, TokenType::Colon,
                                      TokenType::PipePipe, TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 0u);
}

TEST(LexerOperators, QueryBranchHeader) {
    const LexResult result = lex("| prawda =>:");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Pipe, TokenType::KwPrawda, TokenType::FatArrow,
                                      TokenType::Colon, TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 0u);
}

TEST(LexerSigils, HashColonIsReturnTypeSigilNotComment) {
    // '-> #:' annotates a function's return type; '#' glued to ':' must NOT
    // start a comment.
    const LexResult result = lex("-> #: prawda");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Arrow, TokenType::HashSigil, TokenType::Colon,
                                      TokenType::KwPrawda, TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 0u);
}

// --- Integer literals -----------------------------------------------------------

TEST(LexerIntegers, ParsesValue) {
    const LexResult result = lex("0 7 123456789");
    ASSERT_EQ(result.tokens.size(), 4u);
    EXPECT_EQ(result.tokens[0].intValue, 0);
    EXPECT_EQ(result.tokens[1].intValue, 7);
    EXPECT_EQ(result.tokens[2].intValue, 123456789);
}

TEST(LexerIntegers, MaxInt64Fits) {
    const LexResult result = lex("9223372036854775807");
    ASSERT_EQ(result.tokens.size(), 2u);
    EXPECT_EQ(result.tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(result.tokens[0].intValue, std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(result.errorCount, 0u);
}

TEST(LexerIntegers, OverflowIsAnError) {
    const LexResult result = lex("9223372036854775808");
    ASSERT_EQ(result.tokens.size(), 2u);
    EXPECT_EQ(result.tokens[0].type, TokenType::Unknown);
    EXPECT_EQ(result.errorCount, 1u);
}

TEST(LexerIntegers, DigitsGluedToLettersAreMalformed) {
    const LexResult result = lex("12abc");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Unknown, TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 1u);
}

// --- String literals --------------------------------------------------------------

TEST(LexerStrings, SimpleString) {
    const LexResult result = lex("\"Witaj, Nurt!\"");
    ASSERT_EQ(result.tokens.size(), 2u);
    EXPECT_EQ(result.tokens[0].type, TokenType::StringLiteral);
    EXPECT_EQ(result.tokens[0].stringValue, "Witaj, Nurt!");
    EXPECT_EQ(result.tokens[0].lexeme, "\"Witaj, Nurt!\"");
}

TEST(LexerStrings, EscapeSequencesAreDecoded) {
    const LexResult result = lex(R"("linia\n\ttab \"cytat\" \\ukosnik")");
    ASSERT_EQ(result.tokens.size(), 2u);
    EXPECT_EQ(result.tokens[0].type, TokenType::StringLiteral);
    EXPECT_EQ(result.tokens[0].stringValue, "linia\n\ttab \"cytat\" \\ukosnik");
    EXPECT_EQ(result.errorCount, 0u);
}

TEST(LexerStrings, UnknownEscapeIsAnError) {
    const LexResult result = lex(R"("z\q")");
    ASSERT_EQ(result.tokens.size(), 2u);
    EXPECT_EQ(result.tokens[0].type, TokenType::StringLiteral);
    EXPECT_EQ(result.errorCount, 1u);
}

TEST(LexerStrings, UnterminatedAtEndOfFile) {
    const LexResult result = lex("\"nigdy sie nie konczy");
    EXPECT_EQ(result.tokens[0].type, TokenType::Unknown);
    EXPECT_EQ(result.errorCount, 1u);
}

TEST(LexerStrings, UnterminatedAtNewline) {
    const LexResult result = lex("\"urwane\nprawda");
    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{TokenType::Unknown, TokenType::KwPrawda,
                                      TokenType::EndOfFile}));
    EXPECT_EQ(result.errorCount, 1u);
}

// --- Source locations ---------------------------------------------------------------

TEST(LexerLocations, LineAndColumnTracking) {
    const LexResult result = lex("powolaj f()\n    <- 1\nkoniec\n");
    ASSERT_EQ(result.tokens.size(), 8u);

    EXPECT_EQ(result.tokens[0].location.line, 1u);  // powolaj
    EXPECT_EQ(result.tokens[0].location.column, 1u);
    EXPECT_EQ(result.tokens[1].location.line, 1u);  // f
    EXPECT_EQ(result.tokens[1].location.column, 9u);
    EXPECT_EQ(result.tokens[4].location.line, 2u);  // <-
    EXPECT_EQ(result.tokens[4].location.column, 5u);
    EXPECT_EQ(result.tokens[5].location.line, 2u);  // 1
    EXPECT_EQ(result.tokens[5].location.column, 8u);
    EXPECT_EQ(result.tokens[6].location.line, 3u);  // koniec
    EXPECT_EQ(result.tokens[6].location.column, 1u);
    EXPECT_EQ(result.tokens[7].type, TokenType::EndOfFile);
    EXPECT_EQ(result.tokens[7].location.line, 4u);
}

TEST(LexerLocations, CommentsDoNotBreakLineCounting) {
    const LexResult result = lex("# komentarz\n# kolejny\nrob");
    ASSERT_EQ(result.tokens.size(), 2u);
    EXPECT_EQ(result.tokens[0].type, TokenType::KwRob);
    EXPECT_EQ(result.tokens[0].location.line, 3u);
    EXPECT_EQ(result.tokens[0].location.column, 1u);
}

// --- Whole-program smoke test ----------------------------------------------------------

TEST(LexerProgram, FullNurtProgram) {
    constexpr std::string_view program =
        "# suma.nrt\n"
        "powolaj suma(#a, #b)\n"
        "    #a + #b -> #wynik\n"
        "    <- #wynik\n"
        "koniec\n"
        "\n"
        "10 -> #x\n"
        "suma(#x, 32) -> #z\n"
        "dopoki #z > 0 rob\n"
        "    #z - 1 -> #z\n"
        "koniec\n"
        "prawda -> ?gotowe\n"
        "\"Witaj!\" -> $powitanie\n";

    const LexResult result = lex(program);
    EXPECT_EQ(result.errorCount, 0u);

    EXPECT_EQ(typesOf(result.tokens),
              (std::vector<TokenType>{
                  // powolaj suma(#a, #b)
                  TokenType::KwPowolaj, TokenType::Identifier, TokenType::LParen,
                  TokenType::HashSigil, TokenType::Identifier, TokenType::Comma,
                  TokenType::HashSigil, TokenType::Identifier, TokenType::RParen,
                  // #a + #b -> #wynik
                  TokenType::HashSigil, TokenType::Identifier, TokenType::Plus,
                  TokenType::HashSigil, TokenType::Identifier, TokenType::Arrow,
                  TokenType::HashSigil, TokenType::Identifier,
                  // <- #wynik
                  TokenType::ReturnArrow, TokenType::HashSigil, TokenType::Identifier,
                  // koniec
                  TokenType::KwKoniec,
                  // 10 -> #x
                  TokenType::IntegerLiteral, TokenType::Arrow, TokenType::HashSigil,
                  TokenType::Identifier,
                  // suma(#x, 32) -> #z
                  TokenType::Identifier, TokenType::LParen, TokenType::HashSigil,
                  TokenType::Identifier, TokenType::Comma, TokenType::IntegerLiteral,
                  TokenType::RParen, TokenType::Arrow, TokenType::HashSigil,
                  TokenType::Identifier,
                  // dopoki #z > 0 rob
                  TokenType::KwDopoki, TokenType::HashSigil, TokenType::Identifier,
                  TokenType::Greater, TokenType::IntegerLiteral, TokenType::KwRob,
                  // #z - 1 -> #z
                  TokenType::HashSigil, TokenType::Identifier, TokenType::Minus,
                  TokenType::IntegerLiteral, TokenType::Arrow, TokenType::HashSigil,
                  TokenType::Identifier,
                  // koniec
                  TokenType::KwKoniec,
                  // prawda -> ?gotowe
                  TokenType::KwPrawda, TokenType::Arrow, TokenType::QuestionSigil,
                  TokenType::Identifier,
                  // "Witaj!" -> $powitanie
                  TokenType::StringLiteral, TokenType::Arrow, TokenType::DollarSigil,
                  TokenType::Identifier,
                  // EOF
                  TokenType::EndOfFile}));
}

TEST(LexerProgram, NextReturnsEofForever) {
    nurt::DiagnosticEngine diagnostics("test.nrt", "rob");
    nurt::Lexer lexer("rob", diagnostics);
    EXPECT_EQ(lexer.next().type, TokenType::KwRob);
    EXPECT_EQ(lexer.next().type, TokenType::EndOfFile);
    EXPECT_EQ(lexer.next().type, TokenType::EndOfFile);
}
