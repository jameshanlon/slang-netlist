#pragma once

#include "netlist/DriverMap.hpp"
#include "netlist/NetlistNode.hpp"

#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/text/FormatBuffer.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/SmallVector.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace slang::netlist {

/// Map symbols to indexes.
using SymbolSlotMap = std::map<const ast::ValueSymbol *, uint32_t>;

/// Map indexes to symbols.
using SlotSymbolMap = std::vector<const ast::ValueSymbol *>;

/// Per-symbol SymbolDriverMaps.
using SymbolDrivers = std::vector<DriverMap>;

/// Track drivers for symbols.
///
/// Each symbol encountered in the AST has an interval map where each interval
/// is a range that is driven by one or more statements in the design. Intervals
/// are non-overlapping, each interval maps to a list of DriverInfo objects, and
/// adjacent intervals have different driver lists.
///
/// Note that SymbolDrivers is not a member of this class because it is stored
/// in the analysis state during the DataFlowAnalysis pass.
class SymbolTracker {

  BumpAllocator allocator;
  DriverMap::AllocatorType mapAllocator;

  // Map symbols to indexes in vectors of SymbolDriverMaps.
  SymbolSlotMap symbolToSlot;

  // The reverse mapping of slot indexes to symbols.
  SlotSymbolMap slotToSymbol;

public:
  SymbolTracker() : mapAllocator(allocator) {}

  auto begin() const { return symbolToSlot.begin(); }
  auto end() const { return symbolToSlot.end(); }

  /// Get a symbol by its slot index.
  auto getSymbol(uint32_t slot) const -> const ast::ValueSymbol * {
    SLANG_ASSERT(slot < slotToSymbol.size());
    return slotToSymbol[slot];
  }

  /// Get the slot index for a symbol, if it exists.
  auto getSlot(ast::ValueSymbol const &symbol) -> std::optional<uint32_t> {
    auto it = symbolToSlot.find(&symbol);
    if (it != symbolToSlot.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  /// Add a driver for the specified symbol. This overwrites any existing
  /// drivers for the specified bit range.
  void addDriver(SymbolDrivers &drivers, ast::ValueSymbol const &symbol,
                 ast::Expression const *lsp, DriverBitRange bounds,
                 NetlistNode *node, bool merge = false);

  /// Merge a driver for the specified symbol. This adds to any existing
  /// drivers for the specified bit range.
  void mergeDriver(SymbolDrivers &drivers, ast::ValueSymbol const &symbol,
                   ast::Expression const *lsp, DriverBitRange bounds,
                   NetlistNode *node);

  /// Merge a list of drivers for the specified symbol and bit range.
  void mergeDrivers(SymbolDrivers &drivers, ast::ValueSymbol const &symbol,
                    DriverBitRange bounds, DriverList const &driverList);

  /// Return a list of all the drivers for the given symbol and bit range.
  /// If there are no drivers, the returned list will be empty.
  auto getDrivers(SymbolDrivers &drivers, ast::ValueSymbol const &symbol,
                  DriverBitRange bounds) -> DriverList;

  /// Dump the current driver map for all symbols for debugging output.
  auto dumpDrivers(ast::ValueSymbol const &symbol, DriverMap &driverMap)
      -> std::string;

  auto getAllocator() -> DriverMap::AllocatorType & { return mapAllocator; }
};

} // namespace slang::netlist
