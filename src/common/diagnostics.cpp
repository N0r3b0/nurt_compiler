#include "common/diagnostics.hpp"

#include <utility>

namespace nurt {

namespace {

[[nodiscard]] constexpr std::string_view severityName(Severity severity) {
    switch (severity) {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Note:
        return "note";
    }
    return "unknown";
}

} // namespace

DiagnosticEngine::DiagnosticEngine(std::string_view filename, std::string_view source)
    : filename_(filename), source_(source) {}

void DiagnosticEngine::error(SourceLocation location, std::string message) {
    report(Severity::Error, location, std::move(message));
}

void DiagnosticEngine::warning(SourceLocation location, std::string message) {
    report(Severity::Warning, location, std::move(message));
}

void DiagnosticEngine::note(SourceLocation location, std::string message) {
    report(Severity::Note, location, std::move(message));
}

void DiagnosticEngine::report(Severity severity, SourceLocation location, std::string message) {
    if (severity == Severity::Error) {
        ++errorCount_;
    }
    diagnostics_.push_back(Diagnostic{severity, std::move(message), location});
}

std::string_view DiagnosticEngine::lineContent(std::uint32_t line) const {
    std::size_t start = 0;
    for (std::uint32_t current = 1; current < line; ++current) {
        const std::size_t newline = source_.find('\n', start);
        if (newline == std::string_view::npos) {
            return {};
        }
        start = newline + 1;
    }
    std::size_t end = source_.find('\n', start);
    if (end == std::string_view::npos) {
        end = source_.size();
    }
    std::string_view content = source_.substr(start, end - start);
    if (!content.empty() && content.back() == '\r') {
        content.remove_suffix(1);
    }
    return content;
}

void DiagnosticEngine::printAll(std::ostream& out) const {
    for (const Diagnostic& diagnostic : diagnostics_) {
        out << filename_ << ':' << diagnostic.location.line << ':' << diagnostic.location.column
            << ": " << severityName(diagnostic.severity) << ": " << diagnostic.message << '\n';

        const std::string_view line = lineContent(diagnostic.location.line);
        if (!line.empty()) {
            out << "    " << line << '\n';
            out << "    ";
            // Render tabs as tabs so the caret stays aligned with the source line.
            for (std::uint32_t i = 1; i < diagnostic.location.column && i <= line.size(); ++i) {
                out << (line[i - 1] == '\t' ? '\t' : ' ');
            }
            out << "^\n";
        }
    }
}

} // namespace nurt
