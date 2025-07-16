#pragma once

#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/util/IntervalMap.h"

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"

namespace slang::netlist {

using SymbolSlotMap = std::map<const ast::ValueSymbol *, uint32_t>;
using SymbolDriverMap = IntervalMap<uint64_t, NetlistNode *, 8>;

/// Represent the netlist connectivity of an elaborated design.
class NetlistGraph : public DirectedGraph<NetlistNode, NetlistEdge> {

  BumpAllocator allocator;
  SymbolDriverMap::allocator_type mapAllocator;

  // Maps visited symbols to slots in driverMap vector.
  SymbolSlotMap symbolToSlot;

  // For each symbol, map intervals to the netlist node driver.
  std::vector<SymbolDriverMap> driverMap;

public:
  NetlistGraph() : mapAllocator(allocator) {}

  /// Lookup a variable node in the graph by its ValueSymbol and
  /// exact bounds. Return null if a match is not found.
  auto lookupDriver(ast::ValueSymbol const &symbol,
                    std::pair<uint64_t, uint64_t> bounds) -> NetlistNode * {
    if (symbolToSlot.contains(&symbol)) {
      auto &map = driverMap[symbolToSlot[&symbol]];
      for (auto it = map.find(bounds); it != map.end(); it++) {
        if (it.bounds() == bounds) {
          return *it;
        }
      }
    }
    return nullptr;
  }

  /// @brief Merge symbol drivers from a procedural data flow analysis into the
  ///        gloabl driver map for the program.
  /// @param symbolToSlot Mapping from symbols to slot indices.
  /// @param procDriverMap Mapping from ranges to graph nodes.
  auto mergeDrivers(SymbolSlotMap const &procSymbolToSlot,
                    std::vector<SymbolDriverMap> const &procDriverMap) {

    for (auto [symbol, index] : procSymbolToSlot) {

      // Create or retrieve symbol index.
      auto [it, inserted] =
          symbolToSlot.try_emplace(symbol, (uint32_t)symbolToSlot.size());

      // Extend driverMap if necessary.
      auto globalIndex = it->second;
      if (globalIndex >= driverMap.size()) {
        driverMap.emplace_back();
      }

      DEBUG_PRINT("Merging drivers into global map: symbol {} at proc index {} "
                  "global index {}\n",
                  symbol->name, index, globalIndex);
      // Add all the procedure driver intervals to the global map.
      for (auto it = procDriverMap[index].begin();
           it != procDriverMap[index].end(); it++) {
        driverMap[globalIndex].insert(it.bounds(), *it, mapAllocator);
      }
    }
  }

  /// @brief Handle an L-value that is encountered during netlist construction
  /// by
  ///        updating the global driver map.
  /// @param symbol The L value symbol.
  /// @param bounds The range of the symbol that is being assigned to.
  /// @param node   The netlist graph node that is the operation driving the L
  /// value.
  auto handleLvalue(const ast::ValueSymbol &symbol,
                    std::pair<uint32_t, uint32_t> bounds, NetlistNode *node) {
    DEBUG_PRINT("Handle L-value: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);

    // Update visited symbols to slots.
    auto [it, inserted] =
        symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());

    // Update current state definitions.
    auto index = it->second;
    if (index >= driverMap.size()) {
      driverMap.emplace_back();
    }

    driverMap[index].insert(bounds, node, mapAllocator);
  }
};

} // namespace slang::netlist
