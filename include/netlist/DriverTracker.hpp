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
using SymbolSlotMap = std::map<const ast::Symbol *, uint32_t>;

/// Map indexes to symbols.
using SlotSymbolMap = std::vector<const ast::Symbol *>;

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
class DriverTracker {

  BumpAllocator allocator;
  DriverMap::AllocatorType mapAllocator;

  // Map symbols to indexes in vectors of SymbolDriverMaps.
  SymbolSlotMap symbolToSlot;

  // The reverse mapping of slot indexes to symbols.
  SlotSymbolMap slotToSymbol;

public:
  DriverTracker() : mapAllocator(allocator) {}

  /// Add a driver for the specified symbol. This overwrites any existing
  /// drivers for the specified bit range.
  void addDriver(SymbolDrivers &drivers, ast::ValueSymbol const &symbol,
                 ast::Expression const &lsp, DriverBitRange bounds,
                 NetlistNode *node);

  /// Merge a driver for the specified symbol. This adds to any existing
  /// drivers for the specified bit range.
  void mergeDriver(SymbolDrivers &drivers, ast::ValueSymbol const &symbol,
                   ast::Expression const &lsp, DriverBitRange bounds,
                   NetlistNode *node);

  /// Return a list of all the drivers for the given symbol and bit range.
  /// If there are no drivers, the returned list will be empty.
  auto getDrivers(SymbolDrivers &drivers, ast::Symbol const &symbol,
                  DriverBitRange bounds) -> DriverList;

  /// Dump the current driver map for all symbols for debugging output.
  auto dumpDrivers(ast::Symbol const &symbol, DriverMap &driverMap)
      -> std::string;
};

} // namespace slang::netlist
