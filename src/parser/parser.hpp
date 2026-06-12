#pragma once

#include "common/diagnostics.hpp"
#include "lexer/token.hpp"
#include "parser/ast.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nurt {

/// Recursive descent parser for Nurt.
///
/// Grammar summary (see docs/language-spec.md for the full grammar):
///
///   program        := statement*
///   statement      := functionDef | whileLoop | matchStmt | returnStmt | exprLedStmt
///   functionDef    := 'powolaj' IDENT '(' paramList? ')' ('->' SIGIL)? ':'?
///                         statement* 'koniec'
///   whileLoop      := 'dopoki' expression 'rob' ':'? statement* 'koniec'
///   matchStmt      := 'dopasuj' expression
///                         ( '|' caseLiteral '=>' ':' statement* )*
///                         '|' 'inaczej' '=>' ':' statement*
///                     'koniec'
///   caseLiteral    := INTEGER | '-' INTEGER | STRING | 'prawda' | 'falsz'
///   returnStmt     := '<-' expression?
///   exprLedStmt    := expression ( '->' SIGIL IDENT      ; assignment
///                                | '?' queryBranches     ; control flow query
///                                | <nothing> )           ; expression statement
///   queryBranches  := ( '|' ('prawda'|'falsz') '=>' ':' statement* )+ 'koniec'
///
/// Expressions use precedence climbing (Pratt parsing). From loosest to
/// tightest: 'lub', 'i', equality, comparison, additive, multiplicative,
/// unary ('-' '!'), primary. All binary operators are left-associative.
///
/// On a syntax error the parser reports a diagnostic and synchronizes to the
/// next likely statement boundary, so all errors in a file surface in one
/// pass. The returned program contains every statement that parsed cleanly;
/// check DiagnosticEngine::hasErrors() before trusting it for later stages.
///
/// Token lexemes view into the original source buffer, which must therefore
/// outlive the parser AND the returned AST (string values are copied, but
/// diagnostics may reference it).
class Parser {
public:
    Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics);

    /// Parses the whole token stream into a program. Never returns null.
    [[nodiscard]] std::unique_ptr<ProgramNode> parseProgram();

private:
    // --- Token access ---------------------------------------------------------
    [[nodiscard]] const Token& peek(std::size_t lookahead = 0) const;
    [[nodiscard]] const Token& previous() const;
    [[nodiscard]] bool atEnd() const;
    [[nodiscard]] bool check(TokenType type) const;
    const Token& advance();
    bool match(TokenType type);

    /// Consumes the expected token or reports "expected X" and returns nullptr.
    const Token* expect(TokenType type, std::string_view description);

    // --- Statements -----------------------------------------------------------
    [[nodiscard]] StmtPtr parseStatement();
    [[nodiscard]] StmtPtr parseFunctionDef();
    [[nodiscard]] StmtPtr parseWhile();
    [[nodiscard]] StmtPtr parseMatch();
    [[nodiscard]] StmtPtr parseReturn();

    /// A 'dopasuj' case label: a literal of any value type, with an optional
    /// leading '-' for negative integers.
    [[nodiscard]] ExprPtr parseCaseLiteral();

    /// Statements that begin with an expression: assignment, control flow
    /// query, or a bare expression statement.
    [[nodiscard]] StmtPtr parseExpressionLedStatement();

    /// Branches of a control flow query, after the trailing '?' was consumed.
    [[nodiscard]] StmtPtr parseQueryBranches(ExprPtr condition, SourceLocation queryLocation);

    /// Parses statements until 'koniec' (or '|' when stopAtPipe is set).
    /// Does NOT consume the terminator.
    [[nodiscard]] StatementList parseBlock(bool stopAtPipe = false);

    // --- Expressions ----------------------------------------------------------
    [[nodiscard]] ExprPtr parseExpression();
    [[nodiscard]] ExprPtr parseBinary(int minPrecedence);
    [[nodiscard]] ExprPtr parseUnary();
    [[nodiscard]] ExprPtr parsePrimary();
    [[nodiscard]] ExprPtr parseCall(const Token& nameToken);
    [[nodiscard]] ExprPtr parseVariable();

    /// True when the current token can begin an expression.
    [[nodiscard]] bool checkExpressionStart() const;

    // --- Helpers ----------------------------------------------------------------
    /// Maps a sigil token type to the Nurt type it denotes.
    [[nodiscard]] static std::optional<Type> sigilType(TokenType type);

    void errorAt(const Token& token, std::string message);

    /// Skips tokens until a likely statement boundary after a syntax error.
    void synchronize();

    // --- State -------------------------------------------------------------------
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    DiagnosticEngine& diagnostics_;
};

} // namespace nurt
