#pragma once

#include <cstdint>
#include <fmt/format.h>
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

} // namespace slang::netlist
