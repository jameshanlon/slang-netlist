#pragma once

#include "netlist/DriverMap.hpp"
#include "netlist/NetlistNode.hpp"

#include "slang/ast/Expression.h"
#include "slang/ast/symbols/ValueSymbol.h"

#include "slang/util/ConcurrentMap.h"

#include <utility>
#include <vector>

#if defined(SLANG_USE_THREADS)
#include <mutex>
#endif

namespace slang::netlist {

/// Map indexes to value symbols.
using SlotValueMap = std::vector<const ast::ValueSymbol *>;

/// Per-value symbol ValueDriverMaps.
using ValueDrivers = std::vector<DriverMap>;

/// Track drivers for value symbols.
///
/// Each value symbol encountered in the AST has an interval map where each
/// interval is a range that is driven by one or more statements in the design.
/// Intervals are non-overlapping, each interval maps to a list of DriverInfo
/// objects, and adjacent intervals have different driver lists.
///
/// Note that a ValueDrivers variable is not a member of this class because it
/// is stored in the analysis state during the DataFlowAnalysis pass.
class ValueTracker {

  BumpAllocator allocator;
  DriverMap::AllocatorType mapAllocator;

  // Map value symbols to indexes in vectors of ValueDriverMaps.
  concurrent_map<const ast::ValueSymbol *, uint32_t> valueToSlot;

  // The reverse mapping of slot indexes to value symbols.
  SlotValueMap slotToValue;

#if defined(SLANG_USE_THREADS)
  // Protects slotToValue, drivers vector growth, and DriverMap manipulation
  // during concurrent addDrivers calls.
  std::mutex driverMutex;
#endif

public:
  ValueTracker() : mapAllocator(allocator) {}

  /// Visit all symbol-to-slot mappings in insertion order.
  template <typename F> void visitAll(F &&fn) const {
    for (uint32_t i = 0; i < slotToValue.size(); i++) {
      if (slotToValue[i] != nullptr) {
        fn(slotToValue[i], i);
      }
    }
  }

  /// Get a symbol by its slot index.
  auto getSymbol(uint32_t slot) const -> const ast::ValueSymbol * {
    SLANG_ASSERT(slot < slotToValue.size());
    return slotToValue[slot];
  }

  /// Get the slot index for a symbol, if it exists.
  auto getSlot(ast::ValueSymbol const &symbol) -> std::optional<uint32_t> {
    std::optional<uint32_t> result;
    valueToSlot.cvisit(&symbol,
                       [&](const auto &pair) { result = pair.second; });
    return result;
  }

  /// Add a driver for the specified value symbol. This overwrites any existing
  /// drivers for the specified bit range.
  void addDrivers(ValueDrivers &drivers, ast::ValueSymbol const &symbol,
                  DriverBitRange bounds, DriverList const &driverList,
                  bool merge = false);

  /// Return a list of all the drivers for the given value symbol and bit range.
  /// If there are no drivers, the returned list will be empty.
  auto getDrivers(ValueDrivers const &drivers, ast::ValueSymbol const &symbol,
                  DriverBitRange bounds) const -> DriverList;

  /// Dump the current driver map for all value symbols for debugging output.
  static auto dumpDrivers(ast::ValueSymbol const &symbol, DriverMap &driverMap)
      -> std::string;

  auto getAllocator() -> DriverMap::AllocatorType & { return mapAllocator; }
};

} // namespace slang::netlist
