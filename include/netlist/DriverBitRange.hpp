#pragma once

#include "slang/numeric/ConstantValue.h"
#include "slang/util/Util.h"

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

  /// Return true if this range abuts or overlaps @p other: ie. the two
  /// ranges can be combined into a single contiguous range with no gap.
  [[nodiscard]] auto isContiguousWith(DriverBitRange other) const -> bool {
    return upper() + 1 >= other.lower() && other.upper() + 1 >= lower();
  }

  /// Return the union of this range with @p other. Requires the two ranges
  /// to be contiguous (abutting or overlapping).
  [[nodiscard]] auto unionWith(DriverBitRange other) const -> DriverBitRange {
    SLANG_ASSERT(isContiguousWith(other));
    return {std::min(lower(), other.lower()), std::max(upper(), other.upper())};
  }

  /// Return the intersection of this range with @p other, or nullopt if the
  /// ranges do not overlap.
  [[nodiscard]] auto intersection(DriverBitRange other) const
      -> std::optional<DriverBitRange> {
    if (!overlaps(other)) {
      return std::nullopt;
    }
    return DriverBitRange{std::max(lower(), other.lower()),
                          std::min(upper(), other.upper())};
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
