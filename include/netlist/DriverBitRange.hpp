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

  auto toPair() const -> std::pair<int32_t, int32_t> { return {left, right}; }
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
