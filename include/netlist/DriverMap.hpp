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

  using Handle = ExternalManager<DriverList>::Handle;

  using IntervalMapType = IntervalMap<uint32_t, Handle, 8>;

  using AllocatorType = IntervalMapType::allocator_type;

  /// Map driven ranges of a particular symbol to driver list indexes.
  IntervalMapType drivers;

  /// External manager for driver lists.
  ExternalManager<DriverList> driverLists;

  /// Create a deep copy of this DriverMap.
  [[nodiscard]] auto clone(AllocatorType &alloc) const {
    return DriverMap{drivers.clone(alloc), driverLists.clone()};
  }

  /// Get the driver list for the specified handle.
  auto getDriverList(Handle handle) -> DriverList & {
    return driverLists.get(handle);
  }

  /// Insert a new interval mapping to the specified driver list handle.
  auto insert(DriverBitRange bounds, DriverListHandle handle,
              AllocatorType &alloc) -> void {
    drivers.insert(bounds, handle, alloc);
  }

  /// Return an iterator to the beginning of the driver map.
  auto begin() -> typename IntervalMapType::iterator { return drivers.begin(); }

  /// Return an iterator to the end of the driver map.
  auto end() -> typename IntervalMapType::iterator { return drivers.end(); }

  /// Return an iterator to all intervals that overlap the specified bounds.
  auto find(DriverBitRange bounds) ->
      typename IntervalMapType::overlap_iterator {
    return drivers.find(bounds);
  }

  /// Erase the interval at the specified iterator position.
  auto erase(typename IntervalMapType::overlap_iterator it,
             AllocatorType &alloc) -> void {
    drivers.erase(it, alloc);
  }
};

} // namespace slang::netlist
