#include "lexer/lexer.hpp"

#include <charconv>
#include <string>
#include <unordered_map>
#include <utility>

namespace nurt {

namespace {

[[nodiscard]] constexpr bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

[[nodiscard]] constexpr bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

[[nodiscard]] constexpr bool isIdentContinue(char c) {
    return isIdentStart(c) || isDigit(c);
}

[[nodiscard]] const std::unordered_map<std::string_view, TokenType>& keywordTable() {
    static const std::unordered_map<std::string_view, TokenType> table = {
        {"powolaj", TokenType::KwPowolaj},
        {"koniec", TokenType::KwKoniec},
        {"inaczej", TokenType::KwInaczej},
        {"dopoki", TokenType::KwDopoki},
        {"rob", TokenType::KwRob},
        {"prawda", TokenType::KwPrawda},
        {"falsz", TokenType::KwFalsz},
    };
    return table;
}

} // namespace

Lexer::Lexer(std::string_view source, DiagnosticEngine& diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        tokens.push_back(next());
        if (tokens.back().type == TokenType::EndOfFile) {
            break;
        }
    }
    return tokens;
}

Token Lexer::next() {
    skipTrivia();
    beginToken();

    if (atEnd()) {
        return makeToken(TokenType::EndOfFile);
    }

    const char c = advance();

    if (isIdentStart(c)) {
        return lexIdentifierOrKeyword();
    }
    if (isDigit(c)) {
        return lexNumber();
    }

    switch (c) {
    // skipTrivia() only stops on '#' when it is followed by an identifier
    // start or ':', so reaching here means this '#' is the int sigil.
    case '#':
        return makeToken(TokenType::HashSigil);
    case '$':
        return makeToken(TokenType::DollarSigil);
    case '?':
        return makeToken(TokenType::QuestionSigil);
    case '"':
        return lexString();
    case '(':
        return makeToken(TokenType::LParen);
    case ')':
        return makeToken(TokenType::RParen);
    case ',':
        return makeToken(TokenType::Comma);
    case ':':
        return makeToken(TokenType::Colon);
    case '+':
        return makeToken(TokenType::Plus);
    case '*':
        return makeToken(TokenType::Star);
    case '/':
        return makeToken(TokenType::Slash);
    case '%':
        return makeToken(TokenType::Percent);
    case '-':
        return makeToken(match('>') ? TokenType::Arrow : TokenType::Minus);
    case '<':
        if (match('-')) {
            return makeToken(TokenType::ReturnArrow);
        }
        if (match('=')) {
            return makeToken(TokenType::LessEqual);
        }
        return makeToken(TokenType::Less);
    case '>':
        return makeToken(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
    case '!':
        return makeToken(match('=') ? TokenType::BangEqual : TokenType::Bang);
    case '=':
        if (match('=')) {
            return makeToken(TokenType::EqualEqual);
        }
        if (match('>')) {
            return makeToken(TokenType::FatArrow);
        }
        return errorToken("stray '='; Nurt uses '->' for assignment and '==' for comparison");
    case '&':
        if (match('&')) {
            return makeToken(TokenType::AmpAmp);
        }
        return errorToken("stray '&'; did you mean '&&'?");
    case '|':
        if (match('|')) {
            return makeToken(TokenType::PipePipe);
        }
        return makeToken(TokenType::Pipe);
    default:
        return errorToken("unexpected character '" + std::string(1, c) + "'");
    }
}

char Lexer::peekAt(std::size_t lookahead) const {
    const std::size_t index = pos_ + lookahead;
    return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() {
    const char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (atEnd() || source_[pos_] != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::skipTrivia() {
    while (!atEnd()) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }
        if (c == '#') {
            if (isIdentStart(peekAt(1)) || peekAt(1) == ':') {
                // '#x' - sigil reference; '#:' - return type annotation.
                // Either way it is a type sigil, not a comment. Leave it for next().
                return;
            }
            while (!atEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }
        return;
    }
}

void Lexer::beginToken() {
    tokenStart_ = pos_;
    tokenLocation_ = currentLocation();
}

Token Lexer::makeToken(TokenType type) const {
    Token token;
    token.type = type;
    token.lexeme = source_.substr(tokenStart_, pos_ - tokenStart_);
    token.location = tokenLocation_;
    return token;
}

Token Lexer::errorToken(std::string message) {
    diagnostics_.error(tokenLocation_, std::move(message));
    return makeToken(TokenType::Unknown);
}

Token Lexer::lexIdentifierOrKeyword() {
    while (isIdentContinue(peek())) {
        advance();
    }
    const std::string_view lexeme = source_.substr(tokenStart_, pos_ - tokenStart_);
    const auto& keywords = keywordTable();
    const auto it = keywords.find(lexeme);
    return makeToken(it != keywords.end() ? it->second : TokenType::Identifier);
}

Token Lexer::lexNumber() {
    while (isDigit(peek())) {
        advance();
    }

    if (isIdentStart(peek())) {
        // e.g. '12abc' - consume the whole blob so the parser is not fed garbage.
        while (isIdentContinue(peek())) {
            advance();
        }
        const std::string_view lexeme = source_.substr(tokenStart_, pos_ - tokenStart_);
        return errorToken("malformed integer literal '" + std::string(lexeme) + "'");
    }

    Token token = makeToken(TokenType::IntegerLiteral);
    const char* first = token.lexeme.data();
    const char* last = first + token.lexeme.size();
    const std::from_chars_result parsed = std::from_chars(first, last, token.intValue);
    if (parsed.ec == std::errc::result_out_of_range) {
        diagnostics_.error(token.location,
                           "integer literal '" + std::string(token.lexeme) +
                               "' does not fit in a 64-bit int (#)");
        token.type = TokenType::Unknown;
        token.intValue = 0;
    }
    return token;
}

Token Lexer::lexString() {
    std::string value;
    while (true) {
        if (atEnd() || peek() == '\n') {
            return errorToken("unterminated string literal");
        }
        const char c = advance();
        if (c == '"') {
            break;
        }
        if (c == '\\') {
            if (atEnd() || peek() == '\n') {
                return errorToken("unterminated string literal");
            }
            const SourceLocation escapeLocation = currentLocation();
            const char escape = advance();
            switch (escape) {
            case 'n':
                value.push_back('\n');
                break;
            case 't':
                value.push_back('\t');
                break;
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '0':
                value.push_back('\0');
                break;
            default:
                diagnostics_.error(escapeLocation,
                                   "unknown escape sequence '\\" + std::string(1, escape) + "'");
                value.push_back(escape);
                break;
            }
        } else {
            value.push_back(c);
        }
    }

    Token token = makeToken(TokenType::StringLiteral);
    token.stringValue = std::move(value);
    return token;
}

} // namespace nurt
