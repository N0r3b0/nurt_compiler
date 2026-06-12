#include "codegen/codegen.hpp"
#include "common/diagnostics.hpp"
#include "lexer/lexer.hpp"
#include "parser/ast.hpp"
#include "parser/parser.hpp"
#include "semantic/analyzer.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool readFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

void dumpTokens(const std::vector<nurt::Token>& tokens) {
    for (const nurt::Token& token : tokens) {
        std::cout << std::setw(4) << token.location.line << ':' << std::left << std::setw(4)
                  << token.location.column << std::right << "  " << std::left << std::setw(16)
                  << nurt::token_type_name(token.type) << std::right;
        if (token.type != nurt::TokenType::EndOfFile && !token.lexeme.empty()) {
            std::cout << "  '" << token.lexeme << "'";
        }
        std::cout << '\n';
    }
}

[[nodiscard]] std::string defaultOutputPath(const std::string& inputPath) {
    if (inputPath.ends_with(".nrt")) {
        return inputPath.substr(0, inputPath.size() - 4) + ".asm";
    }
    return inputPath + ".asm";
}

void printUsage() {
    std::cerr << "usage: nurtc [options] <file.nrt>\n"
              << "  -o <file>  write the NASM output to <file> (default: <input>.asm)\n"
              << "  --tokens   dump the token stream and stop\n"
              << "  --ast      dump the abstract syntax tree and stop\n";
}

} // namespace

int main(int argc, char** argv) {
    enum class Mode { Compile, Tokens, Ast };
    Mode mode = Mode::Compile;
    std::string path;
    std::string outputPath;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--tokens") {
            mode = Mode::Tokens;
        } else if (arg == "--ast") {
            mode = Mode::Ast;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "nurtc: error: '-o' requires a file argument\n";
                return 2;
            }
            outputPath = argv[++i];
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "nurtc: error: unknown option '" << arg << "'\n";
            printUsage();
            return 2;
        } else if (path.empty()) {
            path = arg;
        } else {
            printUsage();
            return 2;
        }
    }

    if (path.empty()) {
        printUsage();
        return 2;
    }

    if (!path.ends_with(".nrt")) {
        std::cerr << "nurtc: warning: '" << path << "' does not have the .nrt extension\n";
    }

    std::string source;
    if (!readFile(path, source)) {
        std::cerr << "nurtc: error: cannot open file '" << path << "'\n";
        return 2;
    }

    nurt::DiagnosticEngine diagnostics(path, source);

    // --- Lex ---------------------------------------------------------------
    nurt::Lexer lexer(source, diagnostics);
    std::vector<nurt::Token> tokens = lexer.tokenize();

    if (mode == Mode::Tokens) {
        dumpTokens(tokens);
        diagnostics.printAll(std::cerr);
        return diagnostics.hasErrors() ? 1 : 0;
    }

    // --- Parse -------------------------------------------------------------
    nurt::Parser parser(std::move(tokens), diagnostics);
    const std::unique_ptr<nurt::ProgramNode> program = parser.parseProgram();

    if (mode == Mode::Ast) {
        nurt::dump_ast(*program, std::cout);
        diagnostics.printAll(std::cerr);
        return diagnostics.hasErrors() ? 1 : 0;
    }

    if (diagnostics.hasErrors()) {
        diagnostics.printAll(std::cerr);
        return 1;
    }

    // --- Semantic analysis ---------------------------------------------------
    nurt::SemanticAnalyzer analyzer(diagnostics);
    analyzer.analyze(*program);
    if (diagnostics.hasErrors()) {
        diagnostics.printAll(std::cerr);
        return 1;
    }

    // --- Code generation -------------------------------------------------------
    nurt::CodeGenerator codegen(diagnostics);
    const std::string assembly = codegen.generate(*program);
    if (diagnostics.hasErrors()) {
        diagnostics.printAll(std::cerr);
        return 1;
    }

    if (outputPath.empty()) {
        outputPath = defaultOutputPath(path);
    }
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "nurtc: error: cannot write output file '" << outputPath << "'\n";
        return 2;
    }
    out << assembly;
    if (!out.flush()) {
        std::cerr << "nurtc: error: failed writing '" << outputPath << "'\n";
        return 2;
    }

    // Warnings (if any) still get printed on success.
    diagnostics.printAll(std::cerr);
    std::cout << "nurtc: " << path << " -> " << outputPath << '\n';
    return 0;
}
