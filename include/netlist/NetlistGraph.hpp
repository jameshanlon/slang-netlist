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
  /// driver map for the program.
  /// @param symbolToSlot Mapping from symbols to slot indices.
  /// @param procDriverMap Mapping from ranges to graph nodes.
  auto mergeDrivers(SymbolSlotMap const &procSymbolToSlot,
                    std::vector<SymbolDriverMap> const &procDriverMap) {
    // TODO
  }

  /// @brief Called when an L value is encountered during netlist construction.
  /// @param symbol The L value symbol.
  /// @param lsp The expression containing the symbol and its selectors.
  /// @param bounds The range of the symbol that is being assigned to.
  auto handleLvalue(const ast::ValueSymbol &symbol,
                    std::pair<uint32_t, uint32_t> bounds) {
    DEBUG_PRINT("Handle L-value: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);

    //// Update visited symbols to slots.
    // auto [it, inserted] =
    //     symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());

    //// Update current state definitions.
    // auto index = it->second;
    // if (index >= driverMap.size()) {
    //   driverMap.emplace_back();
    // }

    // auto &definitions = driverMap[index];
    // for (auto it = definitions.find(bounds); it != definitions.end();) {

    //  auto itBounds = it.bounds();

    //  // Existing entry completely contains new bounds, so split that entry.
    //  if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {
    //    definitions.erase(it, mapAllocator);
    //    definitions.insert({itBounds.first, bounds.first}, *it, mapAllocator);
    //    definitions.insert({bounds.second, itBounds.second}, *it,
    //    mapAllocator); break;
    //  }

    //  // New bounds completely contain an existing entry, so delete that
    //  entry. if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {
    //    definitions.erase(it, mapAllocator);
    //    it = definitions.find(bounds); // FIXME?
    //  } else {
    //    ++it;
    //  }
    //}

    //// Insert the new definition into the DFA state.
    // definitions.insert(bounds, node, mapAllocator);
  }
};

} // namespace slang::netlist
