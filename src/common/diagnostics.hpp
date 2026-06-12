#pragma once

#include "common/source_location.hpp"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace nurt {

enum class Severity {
    Error,
    Warning,
    Note,
};

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string message;
    SourceLocation location;
};

/// Collects diagnostics emitted by compiler stages and renders them with
/// source context. The engine only *views* the file name and source buffer;
/// both must outlive the engine.
class DiagnosticEngine {
public:
    DiagnosticEngine(std::string_view filename, std::string_view source);

    void error(SourceLocation location, std::string message);
    void warning(SourceLocation location, std::string message);
    void note(SourceLocation location, std::string message);

    [[nodiscard]] bool hasErrors() const { return errorCount_ > 0; }
    [[nodiscard]] std::size_t errorCount() const { return errorCount_; }
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    /// Renders every collected diagnostic as
    ///   file:line:col: severity: message
    ///       <source line>
    ///       ^
    void printAll(std::ostream& out) const;

private:
    void report(Severity severity, SourceLocation location, std::string message);

    /// Returns the full text of the given 1-based source line (without newline).
    [[nodiscard]] std::string_view lineContent(std::uint32_t line) const;

    std::string_view filename_;
    std::string_view source_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t errorCount_ = 0;
};

} // namespace nurt
