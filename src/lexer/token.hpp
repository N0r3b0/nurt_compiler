#pragma once

#include "common/source_location.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace nurt {

enum class TokenType {
    // --- Keywords -----------------------------------------------------------
    KwPowolaj,  ///< 'powolaj' - defines (summons) a function
    KwKoniec,   ///< 'koniec'  - ends a block
    KwInaczej,  ///< 'inaczej' - else branch
    KwDopoki,   ///< 'dopoki'  - while
    KwRob,      ///< 'rob'     - do (opens a loop body)
    KwPrawda,   ///< 'prawda'  - boolean literal true
    KwFalsz,    ///< 'falsz'   - boolean literal false
    KwDopasuj,  ///< 'dopasuj' - multi-branch match statement
    KwI,        ///< 'i'       - logical AND operator
    KwLub,      ///< 'lub'     - logical OR operator

    // --- Type sigils ---------------------------------------------------------
    HashSigil,      ///< '#' - 64-bit signed integer
    DollarSigil,    ///< '$' - string
    QuestionSigil,  ///< '?' - boolean

    // --- Flow operators ------------------------------------------------------
    Arrow,        ///< '->' - assignment, value flows into a variable
    ReturnArrow,  ///< '<-' - return, value flows out of a function
    FatArrow,     ///< '=>' - introduces a branch body in a control flow query

    // --- Literals and names --------------------------------------------------
    Identifier,
    IntegerLiteral,
    StringLiteral,

    // --- Arithmetic operators ------------------------------------------------
    Plus,     ///< '+'
    Minus,    ///< '-'
    Star,     ///< '*'
    Slash,    ///< '/'
    Percent,  ///< '%'

    // --- Comparison operators ------------------------------------------------
    EqualEqual,    ///< '=='
    BangEqual,     ///< '!='
    Less,          ///< '<'
    LessEqual,     ///< '<='
    Greater,       ///< '>'
    GreaterEqual,  ///< '>='

    // --- Logical operators ---------------------------------------------------
    // Binary logical AND/OR are the keywords 'i' and 'lub' (KwI, KwLub).
    Bang,  ///< '!'

    // --- Punctuation ---------------------------------------------------------
    LParen,  ///< '('
    RParen,  ///< ')'
    Comma,   ///< ','
    Colon,   ///< ':' - opens a block body
    Pipe,    ///< '|' - introduces a branch in a control flow query

    // --- Control -------------------------------------------------------------
    EndOfFile,
    Unknown,  ///< lexically invalid input; a diagnostic has been emitted
};

[[nodiscard]] constexpr std::string_view token_type_name(TokenType type) {
    switch (type) {
    case TokenType::KwPowolaj:
        return "KwPowolaj";
    case TokenType::KwKoniec:
        return "KwKoniec";
    case TokenType::KwInaczej:
        return "KwInaczej";
    case TokenType::KwDopoki:
        return "KwDopoki";
    case TokenType::KwRob:
        return "KwRob";
    case TokenType::KwPrawda:
        return "KwPrawda";
    case TokenType::KwFalsz:
        return "KwFalsz";
    case TokenType::KwDopasuj:
        return "KwDopasuj";
    case TokenType::KwI:
        return "KwI";
    case TokenType::KwLub:
        return "KwLub";
    case TokenType::HashSigil:
        return "HashSigil";
    case TokenType::DollarSigil:
        return "DollarSigil";
    case TokenType::QuestionSigil:
        return "QuestionSigil";
    case TokenType::Arrow:
        return "Arrow";
    case TokenType::ReturnArrow:
        return "ReturnArrow";
    case TokenType::FatArrow:
        return "FatArrow";
    case TokenType::Identifier:
        return "Identifier";
    case TokenType::IntegerLiteral:
        return "IntegerLiteral";
    case TokenType::StringLiteral:
        return "StringLiteral";
    case TokenType::Plus:
        return "Plus";
    case TokenType::Minus:
        return "Minus";
    case TokenType::Star:
        return "Star";
    case TokenType::Slash:
        return "Slash";
    case TokenType::Percent:
        return "Percent";
    case TokenType::EqualEqual:
        return "EqualEqual";
    case TokenType::BangEqual:
        return "BangEqual";
    case TokenType::Less:
        return "Less";
    case TokenType::LessEqual:
        return "LessEqual";
    case TokenType::Greater:
        return "Greater";
    case TokenType::GreaterEqual:
        return "GreaterEqual";
    case TokenType::Bang:
        return "Bang";
    case TokenType::LParen:
        return "LParen";
    case TokenType::RParen:
        return "RParen";
    case TokenType::Comma:
        return "Comma";
    case TokenType::Colon:
        return "Colon";
    case TokenType::Pipe:
        return "Pipe";
    case TokenType::EndOfFile:
        return "EndOfFile";
    case TokenType::Unknown:
        return "Unknown";
    }
    return "<invalid>";
}

[[nodiscard]] constexpr bool is_keyword(TokenType type) {
    return type >= TokenType::KwPowolaj && type <= TokenType::KwLub;
}

struct Token {
    TokenType type = TokenType::EndOfFile;

    /// The raw source text of the token. Views into the source buffer owned by
    /// the caller of the Lexer; valid only as long as that buffer lives.
    std::string_view lexeme;

    SourceLocation location;

    /// Parsed value; meaningful only when type == IntegerLiteral.
    std::int64_t intValue = 0;

    /// Decoded value (escape sequences resolved); meaningful only when
    /// type == StringLiteral.
    std::string stringValue;
};

} // namespace nurt
