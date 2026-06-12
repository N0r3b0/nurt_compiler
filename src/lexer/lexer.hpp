#pragma once

#include "common/diagnostics.hpp"
#include "lexer/token.hpp"

#include <string_view>
#include <vector>

namespace nurt {

/// Hand-written single-pass scanner for Nurt source code.
///
/// The lexer only *views* the source buffer; the buffer must outlive both the
/// lexer and every token it produces (token lexemes point into it).
///
/// Lexically invalid input produces a Token of type Unknown alongside an error
/// reported to the DiagnosticEngine; the scan always continues so that all
/// errors in a file are reported in one pass.
class Lexer {
public:
    Lexer(std::string_view source, DiagnosticEngine& diagnostics);

    /// Scans and returns the next token. Returns EndOfFile forever once the
    /// input is exhausted.
    [[nodiscard]] Token next();

    /// Scans the remaining input and returns all tokens, including the final
    /// EndOfFile token.
    [[nodiscard]] std::vector<Token> tokenize();

private:
    // --- Character access ---------------------------------------------------
    [[nodiscard]] bool atEnd() const { return pos_ >= source_.size(); }
    [[nodiscard]] char peek() const { return atEnd() ? '\0' : source_[pos_]; }
    [[nodiscard]] char peekAt(std::size_t lookahead) const;
    char advance();
    bool match(char expected);

    [[nodiscard]] SourceLocation currentLocation() const { return {line_, column_, pos_}; }

    // --- Scanning helpers ----------------------------------------------------
    /// Skips whitespace and comments. Implements the '#' disambiguation rule:
    /// '#' immediately followed by [A-Za-z_] is a type sigil and is NOT
    /// consumed here; any other '#' starts a comment that runs to end of line.
    void skipTrivia();

    /// Records the start of the token about to be scanned.
    void beginToken();

    [[nodiscard]] Token makeToken(TokenType type) const;
    [[nodiscard]] Token errorToken(std::string message);

    [[nodiscard]] Token lexIdentifierOrKeyword();
    [[nodiscard]] Token lexNumber();
    [[nodiscard]] Token lexString();

    // --- State ----------------------------------------------------------------
    std::string_view source_;
    DiagnosticEngine& diagnostics_;

    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;

    std::size_t tokenStart_ = 0;
    SourceLocation tokenLocation_{};
};

} // namespace nurt
