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

/// Represent the netlist connectivity of an elaborated design.
class NetlistGraph : public DirectedGraph<NetlistNode, NetlistEdge> {

  using SymbolDriverMap = IntervalMap<uint64_t, NetlistNode *, 8>;

  BumpAllocator allocator;
  SymbolDriverMap::allocator_type mapAllocator;

  // Maps visited symbols to slots in driverMap vector.
  std::map<const ast::ValueSymbol *, uint32_t> symbolToSlot;

  // For each symbol, a map of intervals and the netlist node driver.
  std::vector<SymbolDriverMap> driverMap;

public:
  NetlistGraph() : mapAllocator(allocator) {}

  ///// Lookup a ValueDriver for the given symbol and bounds.
  ///// Returns std::nullopt if no driver is found.
  //[[nodiscard]] auto getDriver(const ast::ValueSymbol &symbol,
  //                             std::pair<uint32_t, uint32_t> bounds)
  //    -> analysis::ValueDriver const * {
  //  // Get the driver for the symbol at the given bounds.
  //  auto drivers = analysisManager.getDrivers(symbol);
  //  for (auto [driver, bitRange] : drivers) {
  //    if (ConstantRange(bitRange).contains(ConstantRange(bounds))) {
  //      return driver;
  //    }
  //  }
  //  // No driver found for the symbol at the given bounds.
  //  return nullptr;
  //}

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

  /// @brief Called when an R value is encountered during netlist construction.
  /// @param symbol The symbol for the R value.
  /// @param bounds The range of the R value that is being used.
  void handleRvalue(const ast::ValueSymbol &symbol,
                    std::pair<uint32_t, uint32_t> bounds, NetlistNode *node) {
    DEBUG_PRINT("Handle R-value: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);

    if (symbolToSlot.contains(&symbol)) {
      // Symbol is assigned in this procedural block.

      auto index = symbolToSlot.at(&symbol);
      auto &definitions = driverMap[index];

      // auto const *driver = getDriver(symbol, bounds);
      // SLANG_ASSERT(driver);

      for (auto it = definitions.find(bounds); it != definitions.end(); it++) {
        auto itBounds = it.bounds();

        // R-value bounds completely contains a definition bounds.
        if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {
          // Add an edge from the definition node to the current node using it.
          if (node) {
            auto &edge = addEdge(**it, *node);
            edge.setVariable(&symbol, bounds);
          }
        }

        // R-value bounds completely contain a definition bounds.
        if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {
          // Add an edge from the definition node to the current node using it.
          SLANG_ASSERT(node);
          auto &edge = addEdge(**it, *node);
          edge.setVariable(&symbol, bounds);
        }
      }
    } else {
      // Otherwise, the symbol is unknown...
    }
  }

  /// @brief Called when an L value is encountered during netlist construction.
  /// @param symbol The L value symbol.
  /// @param lsp The expression containing the symbol and its selectors.
  /// @param bounds The range of the symbol that is being assigned to.
  auto handleLvalue(const ast::ValueSymbol &symbol, const ast::Expression &lsp,
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

    auto &definitions = driverMap[index];
    for (auto it = definitions.find(bounds); it != definitions.end();) {

      auto itBounds = it.bounds();

      // Existing entry completely contains new bounds, so split entry.
      if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {
        definitions.erase(it, mapAllocator);
        definitions.insert({itBounds.first, bounds.first}, *it, mapAllocator);
        definitions.insert({bounds.second, itBounds.second}, *it, mapAllocator);
        break;
      }

      // New bounds completely contain an existing entry, so delete entry.
      if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {
        definitions.erase(it, mapAllocator);
        it = definitions.find(bounds);
      } else {
        ++it;
      }
    }

    // Insert the new definition into the DFA state.
    definitions.insert(bounds, node, mapAllocator);
  }
};

} // namespace slang::netlist
