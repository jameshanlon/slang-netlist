#pragma once

#include "netlist/DriverBitRange.hpp"
#include "netlist/ExternalManager.hpp"

#include "slang/ast/Expression.h"
#include "slang/util/IntervalMap.h"

#include <cstdint>
#include <fmt/format.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slang::netlist {

class NetlistNode;

/// Information about a driver of a particular range of a symbol.
struct DriverInfo {
  NetlistNode *node;
  const ast::Expression *lsp;

  auto operator==(const DriverInfo &other) const -> bool {
    return node == other.node;
  }

  struct Hash {
    auto operator()(const DriverInfo &info) const -> size_t {
      return std::hash<const void *>()(info.node);
    }
  };
};

/// A list of AST/netlist drivers for a particular range of a symbol.
using DriverList = std::unordered_set<DriverInfo, DriverInfo::Hash>;

/// An identifier held by the interval map corresponding to the
/// separately-allocated driver list.
using DriverListHandle = uint32_t;

/// Map driven ranges of a particular symbol to driver lists.
/// Each interval maps to a handle that is used to look up the actual
/// DriverList that is managed separately by an ExternalManager.
struct DriverMap {

  using Handle = ExternalManager<DriverList>::Handle;

  using IntervalMapType = IntervalMap<int32_t, Handle>;

  using AllocatorType = IntervalMapType::allocator_type;

  /// Map driven ranges of a particular symbol to driver list indexes.
  IntervalMapType driverIntervals;

  /// External manager for driver lists.
  ExternalManager<DriverList> driverLists;

  /// Create a deep copy of this DriverMap.
  [[nodiscard]] auto clone(AllocatorType &alloc) const {
    return DriverMap{.driverIntervals = driverIntervals.clone(alloc),
                     .driverLists = driverLists.clone()};
  }

  /// Create a DriverList and return its handle.
  auto newDriverList() -> Handle { return driverLists.allocate(); }

  /// Add a DriverList by copying in the contents and return its new handle.
  auto addDriverList(DriverList const &list) -> Handle {
    auto handle = driverLists.allocate();
    driverLists.get(handle) = list;
    return handle;
  }

  /// Get the driver list for the specified handle.
  auto getDriverList(Handle handle) -> DriverList & {
    return driverLists.get(handle);
  }

  /// Get the driver list for the specified handle.
  auto getDriverList(Handle handle) const -> DriverList const & {
    return driverLists.get(handle);
  }

  /// Insert a new interval mapping to the specified driver list handle.
  auto insert(DriverBitRange bounds, DriverListHandle handle,
              AllocatorType &alloc) -> void {
    driverIntervals.insert(bounds.toPair(), handle, alloc);
  }

  /// Return an iterator to the beginning of the driver map.
  auto begin() -> typename IntervalMapType::iterator {
    return driverIntervals.begin();
  }

  /// Return an iterator to the beginning of the driver map.
  auto begin() const -> typename IntervalMapType::const_iterator {
    return driverIntervals.begin();
  }

  /// Return an iterator to the end of the driver map.
  auto end() -> typename IntervalMapType::iterator {
    return driverIntervals.end();
  }

  /// Return an iterator to the end of the driver map.
  auto end() const -> typename IntervalMapType::const_iterator {
    return driverIntervals.end();
  }

  /// Return an iterator to all intervals that overlap the specified bounds.
  auto find(DriverBitRange bounds) const ->
      typename IntervalMapType::overlap_iterator {
    return driverIntervals.find(bounds.toPair());
  }

  /// Check whether the driver map is empty.
  [[nodiscard]]
  auto empty() const -> bool {
    return driverIntervals.empty();
  }

  /// Erase the interval at the specified iterator position.
  auto erase(typename IntervalMapType::overlap_iterator it,
             AllocatorType &alloc) -> void {
    driverIntervals.erase(it, alloc);
  }

  /// Erase the driver list with the specified handle.
  auto erase(DriverListHandle handle) -> void { driverLists.erase(handle); }
};

} // namespace slang::netlist
