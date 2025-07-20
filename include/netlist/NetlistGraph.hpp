#pragma once

#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/util/IntervalMap.h"

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"

namespace slang::netlist {

using SymbolSlotMap = std::map<const ast::ValueSymbol *, uint32_t>;
using SymbolDriverMap = IntervalMap<uint64_t, NetlistNode *, 8>;

struct PendingRvalue {
  const ast::ValueSymbol *symbol;
  std::pair<uint64_t, uint64_t> bounds;
  NetlistNode *node{nullptr};

  PendingRvalue(const ast::ValueSymbol *symbol,
                std::pair<uint64_t, uint64_t> bounds, NetlistNode *node)
      : symbol(symbol), bounds(bounds), node(node) {}
};

/// Represent the netlist connectivity of an elaborated design.
class NetlistGraph : public DirectedGraph<NetlistNode, NetlistEdge> {

  BumpAllocator allocator;
  SymbolDriverMap::allocator_type mapAllocator;

  // Maps visited symbols to slots in driverMap vector.
  SymbolSlotMap symbolToSlot;

  // For each symbol, map intervals to the netlist node that is driving the
  // interval.
  std::vector<SymbolDriverMap> driverMap;

  // Map symbols to ports.
  std::map<ast::Symbol const *, Port *> portMap;

  // Pending R-values that need to be connected after the main AST traversal.
  std::vector<PendingRvalue> pendingRValues;

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

  /// Lookup a variable node in the graph by its hierarchical name.
  auto lookup(std::string_view name) -> NetlistNode * {
    // TODO.
    return nullptr;
  }

  /// @brief Add a pending R-value to the list of R-values to be processed.
  auto addRvalue(const ast::ValueSymbol *symbol,
                 std::pair<uint64_t, uint64_t> bounds,
                 NetlistNode *node) -> void {
    DEBUG_PRINT("Adding pending R-value: {} [{}:{}]\n", symbol->name,
                bounds.first, bounds.second);
    SLANG_ASSERT(symbol != nullptr && "Symbol must not be null");
    SLANG_ASSERT(node != nullptr && "Node must not be null");
    pendingRValues.emplace_back(symbol, bounds, node);
  }

  /// @brief Process pending R-values after the main AST traversal.
  ///
  /// Connects the pending R-values to their respective nodes in the netlist
  /// graph. This is necessary to ensure that all drivers are processed before
  /// handling R-values, as they may depend on the drivers being present in the
  /// graph. This method should be called after the main AST traversal is
  /// complete.
  void processPendingRvalues() {
    for (auto &pending : pendingRValues) {
      DEBUG_PRINT("Processing pending R-value: {} [{}:{}]\n",
                  pending.symbol->name, pending.bounds.first,
                  pending.bounds.second);
      if (pending.node) {
        auto *driver = lookupDriver(*pending.symbol, pending.bounds);
        SLANG_ASSERT(driver != nullptr &&
                     "Driver for pending R-value must not be null");
        SLANG_ASSERT(pending.node != nullptr &&
                     "R-value node target must not be null");
        auto &edge = addEdge(*driver, *pending.node);

        edge.setVariable(pending.symbol, pending.bounds);
      }
    }
    pendingRValues.clear();
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

        // Add dependencies from drivers of port symbols to the port netlist
        // node.
        if (portMap.contains(symbol) && portMap[symbol]->isOutput()) {
          auto &edge = addEdge(**it, *portMap[symbol]);
          edge.setVariable(symbol, it.bounds());
        }
      }
    }
  }

  /// @brief Handle an L-value that is encountered during netlist construction
  ///        by updating the global driver map.
  /// @param symbol The L value symbol.
  /// @param bounds The range of the symbol that is being assigned to.
  /// @param node   The netlist graph node that is the operation driving the L
  ///               value.
  auto handleLvalue(const ast::ValueSymbol &symbol,
                    std::pair<uint32_t, uint32_t> bounds, NetlistNode *node) {
    DEBUG_PRINT("Handle global lvalue: {} [{}:{}]\n", symbol.name, bounds.first,
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

  /// @brief Create a port node in the netlist.
  /// @param symbol
  void addPort(ast::PortSymbol const &symbol) {

    // Create a node to represent the port.
    auto &node = addNode(
        std::make_unique<Port>(symbol.direction, symbol.internalSymbol));

    // Map internal symbol to a port node.
    portMap[symbol.internalSymbol] = &node.as<Port>();
  }

  /// @brief Lookup a port netlist node by the internal symbol the port is
  ///        connected to.
  /// @param symbol
  /// @return
  [[nodiscard]] auto
  getPort(ast::Symbol const *symbol) -> std::optional<NetlistNode *> {
    if (portMap.contains(symbol)) {
      return portMap[symbol];
    }
    return std::nullopt;
  }

  /// @brief Connect an input port by tracking that it is a driver for the
  ///        internal symbol it is bound to.
  /// @param symbol The internal symbol.
  /// @param bounds The bounds of the internal symbol that are driven.
  ///               This is the whole type range for ports.
  void connectInputPort(ast::ValueSymbol const &symbol,
                        std::pair<uint64_t, uint64_t> bounds) {
    handleLvalue(symbol, bounds, portMap[&symbol]);
  }
};

} // namespace slang::netlist
