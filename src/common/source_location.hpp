#pragma once

#include <cstddef>
#include <cstdint>

namespace nurt {

/// A position inside a Nurt source file. Lines and columns are 1-based;
/// `offset` is the 0-based byte offset into the source buffer.
struct SourceLocation {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::size_t offset = 0;
};

} // namespace nurt
