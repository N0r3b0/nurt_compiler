#include "parser/parser.hpp"

#include <algorithm>
#include <utility>

namespace nurt {

namespace {

struct BinaryOpInfo {
    BinaryOp op;
    int precedence;  ///< higher binds tighter; all Nurt binary operators are left-associative
};

/// Precedence table for the Pratt-style expression parser.
[[nodiscard]] std::optional<BinaryOpInfo> binaryOpInfo(TokenType type) {
    switch (type) {
    case TokenType::PipePipe:
        return BinaryOpInfo{BinaryOp::Or, 1};
    case TokenType::AmpAmp:
        return BinaryOpInfo{BinaryOp::And, 2};
    case TokenType::EqualEqual:
        return BinaryOpInfo{BinaryOp::Equal, 3};
    case TokenType::BangEqual:
        return BinaryOpInfo{BinaryOp::NotEqual, 3};
    case TokenType::Less:
        return BinaryOpInfo{BinaryOp::Less, 4};
    case TokenType::LessEqual:
        return BinaryOpInfo{BinaryOp::LessEqual, 4};
    case TokenType::Greater:
        return BinaryOpInfo{BinaryOp::Greater, 4};
    case TokenType::GreaterEqual:
        return BinaryOpInfo{BinaryOp::GreaterEqual, 4};
    case TokenType::Plus:
        return BinaryOpInfo{BinaryOp::Add, 5};
    case TokenType::Minus:
        return BinaryOpInfo{BinaryOp::Subtract, 5};
    case TokenType::Star:
        return BinaryOpInfo{BinaryOp::Multiply, 6};
    case TokenType::Slash:
        return BinaryOpInfo{BinaryOp::Divide, 6};
    case TokenType::Percent:
        return BinaryOpInfo{BinaryOp::Modulo, 6};
    default:
        return std::nullopt;
    }
}

[[nodiscard]] BuiltinKind builtinKindFor(std::string_view callee) {
    if (callee == "pisz") {
        return BuiltinKind::Pisz;
    }
    if (callee == "bierz") {
        return BuiltinKind::Bierz;
    }
    return BuiltinKind::None;
}

} // namespace

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {
    if (tokens_.empty() || tokens_.back().type != TokenType::EndOfFile) {
        Token eof;
        eof.type = TokenType::EndOfFile;
        if (!tokens_.empty()) {
            eof.location = tokens_.back().location;
        }
        tokens_.push_back(std::move(eof));
    }
}

std::unique_ptr<ProgramNode> Parser::parseProgram() {
    auto program = std::make_unique<ProgramNode>(peek().location);
    while (!atEnd()) {
        if (StmtPtr statement = parseStatement()) {
            program->statements.push_back(std::move(statement));
        } else {
            synchronize();
        }
    }
    return program;
}

// --- Token access ----------------------------------------------------------------

const Token& Parser::peek(std::size_t lookahead) const {
    const std::size_t index = std::min(pos_ + lookahead, tokens_.size() - 1);
    return tokens_[index];
}

const Token& Parser::previous() const {
    return tokens_[pos_ == 0 ? 0 : pos_ - 1];
}

bool Parser::atEnd() const {
    return peek().type == TokenType::EndOfFile;
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

const Token& Parser::advance() {
    const Token& token = peek();
    if (!atEnd()) {
        ++pos_;
    }
    return token;
}

bool Parser::match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

const Token* Parser::expect(TokenType type, std::string_view description) {
    if (check(type)) {
        return &advance();
    }
    errorAt(peek(), "expected " + std::string(description));
    return nullptr;
}

// --- Statements --------------------------------------------------------------------

StmtPtr Parser::parseStatement() {
    // Stray block delimiters: report, consume, and keep parsing so one stray
    // token does not swallow the statements that follow it.
    while (check(TokenType::KwKoniec) || check(TokenType::Pipe)) {
        const Token& stray = peek();
        errorAt(stray, stray.type == TokenType::KwKoniec
                           ? std::string("stray 'koniec' with no open block")
                           : std::string("stray '|' outside a control flow query"));
        advance();
        if (atEnd()) {
            return nullptr;
        }
    }

    switch (peek().type) {
    case TokenType::KwPowolaj:
        return parseFunctionDef();
    case TokenType::KwDopoki:
        return parseWhile();
    case TokenType::ReturnArrow:
        return parseReturn();
    case TokenType::EndOfFile:
        return nullptr;
    default:
        break;
    }

    if (checkExpressionStart()) {
        return parseExpressionLedStatement();
    }

    errorAt(peek(), "expected a statement");
    advance();
    return nullptr;
}

StmtPtr Parser::parseFunctionDef() {
    const SourceLocation location = peek().location;
    advance();  // 'powolaj'

    const Token* name = expect(TokenType::Identifier, "a function name after 'powolaj'");
    if (name == nullptr) {
        return nullptr;
    }

    if (expect(TokenType::LParen, "'(' after the function name") == nullptr) {
        return nullptr;
    }

    std::vector<Parameter> parameters;
    if (!check(TokenType::RParen)) {
        do {
            const std::optional<Type> parameterType = sigilType(peek().type);
            if (!parameterType) {
                errorAt(peek(), "expected a parameter sigil ('#', '$' or '?')");
                return nullptr;
            }
            const SourceLocation parameterLocation = advance().location;
            const Token* parameterName =
                expect(TokenType::Identifier, "a parameter name after the sigil");
            if (parameterName == nullptr) {
                return nullptr;
            }
            parameters.push_back(
                Parameter{*parameterType, std::string(parameterName->lexeme), parameterLocation});
        } while (match(TokenType::Comma));
    }

    if (expect(TokenType::RParen, "')' after the parameter list") == nullptr) {
        return nullptr;
    }

    Type returnType = Type::Void;
    if (match(TokenType::Arrow)) {
        const std::optional<Type> annotated = sigilType(peek().type);
        if (!annotated) {
            errorAt(peek(), "expected a return type sigil ('#:', '$:' or '?:') after '->'");
            return nullptr;
        }
        advance();
        returnType = *annotated;
    }
    match(TokenType::Colon);  // the block-opening ':' is optional

    StatementList body = parseBlock();
    if (expect(TokenType::KwKoniec, "'koniec' to close the function body") == nullptr) {
        return nullptr;
    }

    return std::make_unique<FunctionDefNode>(std::string(name->lexeme), std::move(parameters),
                                             returnType, std::move(body), location);
}

StmtPtr Parser::parseWhile() {
    const SourceLocation location = peek().location;
    advance();  // 'dopoki'

    ExprPtr condition = parseExpression();
    if (!condition) {
        return nullptr;
    }

    if (expect(TokenType::KwRob, "'rob' after the loop condition") == nullptr) {
        return nullptr;
    }
    match(TokenType::Colon);  // the block-opening ':' is optional

    StatementList body = parseBlock();
    if (expect(TokenType::KwKoniec, "'koniec' to close the 'dopoki' loop") == nullptr) {
        return nullptr;
    }

    return std::make_unique<WhileNode>(std::move(condition), std::move(body), location);
}

StmtPtr Parser::parseReturn() {
    const SourceLocation location = peek().location;
    advance();  // '<-'

    ExprPtr value;
    if (checkExpressionStart()) {
        value = parseExpression();
        if (!value) {
            return nullptr;
        }
    }
    return std::make_unique<ReturnNode>(std::move(value), location);
}

StmtPtr Parser::parseExpressionLedStatement() {
    const SourceLocation location = peek().location;

    ExprPtr expression = parseExpression();
    if (!expression) {
        return nullptr;
    }

    // '[expression] -> #target' - the inverted Nurt assignment.
    if (match(TokenType::Arrow)) {
        const std::optional<Type> targetType = sigilType(peek().type);
        if (!targetType) {
            errorAt(peek(),
                    "expected an assignment target ('#name', '$name' or '?name') after '->'");
            return nullptr;
        }
        const SourceLocation targetLocation = advance().location;
        const Token* targetName =
            expect(TokenType::Identifier, "a variable name after the target sigil");
        if (targetName == nullptr) {
            return nullptr;
        }
        return std::make_unique<AssignmentNode>(std::move(expression), *targetType,
                                                std::string(targetName->lexeme), targetLocation,
                                                location);
    }

    // '[expression] ?' opens a control flow query. A '?' that begins a
    // variable reference is always consumed together with its identifier by
    // parsePrimary, so a '?' NOT followed by an identifier is the query marker.
    if (check(TokenType::QuestionSigil) && peek(1).type != TokenType::Identifier) {
        const SourceLocation queryLocation = advance().location;
        return parseQueryBranches(std::move(expression), queryLocation);
    }

    return std::make_unique<ExpressionStatementNode>(std::move(expression), location);
}

StmtPtr Parser::parseQueryBranches(ExprPtr condition, SourceLocation queryLocation) {
    StatementList trueBranch;
    StatementList falseBranch;
    bool sawPrawda = false;
    bool sawFalsz = false;

    if (!check(TokenType::Pipe)) {
        errorAt(peek(), "expected '|' to open a branch after the '?' query");
        return nullptr;
    }

    while (match(TokenType::Pipe)) {
        const Token& label = peek();
        bool isPrawda = false;
        if (match(TokenType::KwPrawda)) {
            isPrawda = true;
        } else if (match(TokenType::KwFalsz)) {
            isPrawda = false;
        } else {
            errorAt(label, "expected 'prawda' or 'falsz' after '|'");
            return nullptr;
        }

        if ((isPrawda && sawPrawda) || (!isPrawda && sawFalsz)) {
            errorAt(label, std::string("duplicate '") + (isPrawda ? "prawda" : "falsz") +
                               "' branch in control flow query");
        }

        if (expect(TokenType::FatArrow, "'=>' after the branch label") == nullptr) {
            return nullptr;
        }
        if (expect(TokenType::Colon, "':' after '=>'") == nullptr) {
            return nullptr;
        }

        StatementList statements = parseBlock(/*stopAtPipe=*/true);
        if (isPrawda) {
            sawPrawda = true;
            trueBranch = std::move(statements);
        } else {
            sawFalsz = true;
            falseBranch = std::move(statements);
        }
    }

    if (expect(TokenType::KwKoniec, "'koniec' to close the control flow query") == nullptr) {
        return nullptr;
    }

    return std::make_unique<IfStatementNode>(std::move(condition), std::move(trueBranch),
                                             std::move(falseBranch), queryLocation);
}

StatementList Parser::parseBlock(bool stopAtPipe) {
    StatementList statements;
    while (!atEnd() && !check(TokenType::KwKoniec) && !(stopAtPipe && check(TokenType::Pipe))) {
        if (StmtPtr statement = parseStatement()) {
            statements.push_back(std::move(statement));
        } else {
            synchronize();
        }
    }
    return statements;
}

// --- Expressions ----------------------------------------------------------------------

ExprPtr Parser::parseExpression() {
    return parseBinary(1);
}

ExprPtr Parser::parseBinary(int minPrecedence) {
    ExprPtr lhs = parseUnary();
    if (!lhs) {
        return nullptr;
    }

    while (true) {
        const std::optional<BinaryOpInfo> info = binaryOpInfo(peek().type);
        if (!info || info->precedence < minPrecedence) {
            return lhs;
        }
        const SourceLocation operatorLocation = advance().location;
        // 'precedence + 1' makes every level left-associative.
        ExprPtr rhs = parseBinary(info->precedence + 1);
        if (!rhs) {
            return nullptr;
        }
        lhs = std::make_unique<BinaryNode>(info->op, std::move(lhs), std::move(rhs),
                                           operatorLocation);
    }
}

ExprPtr Parser::parseUnary() {
    if (check(TokenType::Minus) || check(TokenType::Bang)) {
        const Token& operatorToken = peek();
        const UnaryOp op =
            operatorToken.type == TokenType::Minus ? UnaryOp::Negate : UnaryOp::Not;
        const SourceLocation location = operatorToken.location;
        advance();
        ExprPtr operand = parseUnary();
        if (!operand) {
            return nullptr;
        }
        return std::make_unique<UnaryNode>(op, std::move(operand), location);
    }
    return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
    const Token& token = peek();
    switch (token.type) {
    case TokenType::IntegerLiteral:
        advance();
        return std::make_unique<IntegerLiteralNode>(token.intValue, token.location);
    case TokenType::StringLiteral:
        advance();
        return std::make_unique<StringLiteralNode>(token.stringValue, token.location);
    case TokenType::KwPrawda:
        advance();
        return std::make_unique<BoolLiteralNode>(true, token.location);
    case TokenType::KwFalsz:
        advance();
        return std::make_unique<BoolLiteralNode>(false, token.location);
    case TokenType::HashSigil:
    case TokenType::DollarSigil:
    case TokenType::QuestionSigil:
        return parseVariable();
    case TokenType::Identifier: {
        const Token& name = advance();
        return parseCall(name);
    }
    case TokenType::LParen: {
        advance();
        ExprPtr inner = parseExpression();
        if (!inner) {
            return nullptr;
        }
        if (expect(TokenType::RParen, "')' to close the parenthesized expression") == nullptr) {
            return nullptr;
        }
        return inner;
    }
    default:
        errorAt(token, "expected an expression");
        return nullptr;
    }
}

ExprPtr Parser::parseVariable() {
    const Token& sigil = advance();
    const Type type = *sigilType(sigil.type);
    const Token* name = expect(TokenType::Identifier, "a variable name after the type sigil");
    if (name == nullptr) {
        return nullptr;
    }
    return std::make_unique<VariableNode>(type, std::string(name->lexeme), sigil.location);
}

ExprPtr Parser::parseCall(const Token& nameToken) {
    if (expect(TokenType::LParen,
               "'(' after the function name (bare identifiers are not valid; "
               "variable references need a sigil)") == nullptr) {
        return nullptr;
    }

    std::vector<ExprPtr> arguments;
    if (!check(TokenType::RParen)) {
        do {
            ExprPtr argument = parseExpression();
            if (!argument) {
                return nullptr;
            }
            arguments.push_back(std::move(argument));
        } while (match(TokenType::Comma));
    }

    if (expect(TokenType::RParen, "')' after the call arguments") == nullptr) {
        return nullptr;
    }

    return std::make_unique<CallNode>(std::string(nameToken.lexeme),
                                      builtinKindFor(nameToken.lexeme), std::move(arguments),
                                      nameToken.location);
}

bool Parser::checkExpressionStart() const {
    switch (peek().type) {
    case TokenType::IntegerLiteral:
    case TokenType::StringLiteral:
    case TokenType::KwPrawda:
    case TokenType::KwFalsz:
    case TokenType::Identifier:
    case TokenType::HashSigil:
    case TokenType::DollarSigil:
    case TokenType::QuestionSigil:
    case TokenType::LParen:
    case TokenType::Minus:
    case TokenType::Bang:
        return true;
    default:
        return false;
    }
}

// --- Helpers ------------------------------------------------------------------------------

std::optional<Type> Parser::sigilType(TokenType type) {
    switch (type) {
    case TokenType::HashSigil:
        return Type::Int;
    case TokenType::DollarSigil:
        return Type::String;
    case TokenType::QuestionSigil:
        return Type::Bool;
    default:
        return std::nullopt;
    }
}

void Parser::errorAt(const Token& token, std::string message) {
    if (token.type == TokenType::EndOfFile) {
        message += ", but reached the end of the file";
    } else {
        message += ", got '";
        message += token.lexeme;
        message += '\'';
    }
    diagnostics_.error(token.location, std::move(message));
}

void Parser::synchronize() {
    // Stop at tokens that reliably begin or delimit a statement. Tokens that
    // ARE such boundaries when parseStatement fails are consumed by
    // parseStatement itself, so this loop always makes progress.
    while (!atEnd()) {
        switch (peek().type) {
        case TokenType::KwPowolaj:
        case TokenType::KwDopoki:
        case TokenType::ReturnArrow:
        case TokenType::KwKoniec:
        case TokenType::Pipe:
            return;
        default:
            advance();
        }
    }
}

} // namespace nurt
