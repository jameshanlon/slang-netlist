#pragma once

#include <cstdint>
#include <fmt/format.h>
#include <optional>
#include <string>

namespace slang::netlist {

/// A range over which a symbol is driven.
using DriverBitRange = std::pair<uint32_t, uint32_t>;

static inline auto toString(DriverBitRange const &range) -> std::string {
  if (range.first == range.second) {
    return fmt::format("[{}]", range.first);
  }
  return fmt::format("[{}:{}]", range.second, range.first);
}

/// Compute the intersection of two driver bit ranges.
static inline auto intersectBounds(DriverBitRange const &a,
                                   DriverBitRange const &b)
    -> std::optional<DriverBitRange> {
  uint32_t start = std::max(a.first, b.first);
  uint32_t end = std::min(a.second, b.second);
  if (start <= end) {
    return DriverBitRange{start, end};
  }
  return std::nullopt;
}

} // namespace slang::netlist
