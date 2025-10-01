#pragma once

#include "netlist/DriverMap.hpp"
#include "netlist/ExternalManager.hpp"

#include "slang/ast/Expression.h"
#include "slang/util/IntervalMap.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace slang::netlist {

class NetlistNode;

/// Information about a driver of a particular range of a symbol.
struct DriverInfo {
  NetlistNode *node;
  const ast::Expression *lsp;
};

/// A list of drivers for a particular range of a symbol.
using DriverList = std::vector<DriverInfo>;

/// An identifier held by the interval map corresponding to the
/// separately-allocated driver list.
using DriverListHandle = uint32_t;

/// A range over which a symbol is driven.
using DriverBitRange = std::pair<uint32_t, uint32_t>;

/// Map driven ranges of a particular symbol to driver lists.
/// Each interval maps to a handle that is used to look up the actual
/// DriverList that is managed separately by an ExternalManager.
struct DriverMap {
  using IntervalMapType =
      IntervalMap<uint32_t, ExternalManager<DriverList>::Handle, 8>;

  using AllocatorType = IntervalMapType::allocator_type;

  /// Map driven ranges of a particular symbol to driver list indexes.
  IntervalMapType drivers;

  /// External manager for driver lists.
  ExternalManager<DriverList> driverLists;

  /// Create a deep copy of this DriverMap.
  [[nodiscard]] auto clone(AllocatorType &alloc) {
    return DriverMap{drivers.clone(alloc), driverLists.clone()};
  }
};

} // namespace slang::netlist
