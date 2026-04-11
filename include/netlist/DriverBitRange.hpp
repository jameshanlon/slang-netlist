#pragma once

#include "slang/numeric/ConstantValue.h"

#include <cstdint>
#include <fmt/format.h>
#include <optional>
#include <string>

namespace slang::netlist {

/// A range over which a symbol is driven.
struct DriverBitRange : public ConstantRange {
  using ConstantRange::ConstantRange;

  auto toPair() const -> std::pair<int32_t, int32_t> {
    return {lower(), upper()};
  }

  /// Return the smallest range containing both this range and @p other.
  [[nodiscard]] auto hull(DriverBitRange other) const -> DriverBitRange {
    return {std::min(lower(), other.lower()), std::max(upper(), other.upper())};
  }

  /// Return the intersection of this range with @p other, or nullopt if the
  /// ranges do not overlap.
  [[nodiscard]] auto clipTo(DriverBitRange other) const
      -> std::optional<DriverBitRange> {
    auto lo = std::max(lower(), other.lower());
    auto hi = std::min(upper(), other.upper());
    if (lo > hi) {
      return std::nullopt;
    }
    return DriverBitRange{lo, hi};
  }
};

static inline auto toString(DriverBitRange const &range) -> std::string {
  if (range.lower() == range.upper()) {
    return fmt::format("[{}]", range.lower());
  }
  return fmt::format("[{}:{}]", range.upper(), range.lower());
}

static inline auto toString(std::pair<int32_t, int32_t> bounds) -> std::string {
  return toString(DriverBitRange{bounds.first, bounds.second});
}

} // namespace slang::netlist
