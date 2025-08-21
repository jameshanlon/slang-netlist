#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"

#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/util/IntervalMap.h"

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

  friend class NetlistVisitor;
  friend class DataFlowAnalysis;

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

  /// Lookup a variable node in the graph by its ValueSymbol and
  /// exact bounds. Return null if a match is not found.
  auto lookupDriver(ast::ValueSymbol const &symbol,
                    std::pair<uint64_t, uint64_t> bounds) -> NetlistNode * {
    if (symbolToSlot.contains(&symbol)) {
      auto &map = driverMap[symbolToSlot[&symbol]];
      for (auto it = map.find(bounds); it != map.end(); it++) {
        // if (it.bounds() == bounds) {
        if (ConstantRange(it.bounds()).contains(ConstantRange(bounds))) {
          return *it;
        }
      }
    }
    return nullptr;
  }

  /// Add an R-value to a pending list to be processed once all drivers have
  /// been visited.
  auto addRvalue(const ast::ValueSymbol *symbol,
                 std::pair<uint64_t, uint64_t> bounds, NetlistNode *node)
      -> void {
    DEBUG_PRINT("Adding pending R-value: {} [{}:{}]\n", symbol->name,
                bounds.first, bounds.second);
    SLANG_ASSERT(symbol != nullptr && "Symbol must not be null");
    pendingRValues.emplace_back(symbol, bounds, node);
  }

protected:
  /// Process pending R-values after the main AST traversal.
  ///
  /// This connects the pending R-values to their respective nodes in the
  /// netlist graph. This is necessary to ensure that all drivers are processed
  /// before handling R-values, as they may depend on the drivers being present
  /// in the graph. This method should be called after the main AST traversal is
  /// complete.
  void processPendingRvalues() {
    for (auto &pending : pendingRValues) {
      DEBUG_PRINT("Processing pending R-value: {} [{}:{}]\n",
                  pending.symbol->name, pending.bounds.first,
                  pending.bounds.second);
      if (pending.node) {
        auto *driver = lookupDriver(*pending.symbol, pending.bounds);
        if (driver == nullptr) {
          DEBUG_PRINT("No driver found for pending R-value: {} [{}:{}]\n",
                      pending.symbol->name, pending.bounds.first,
                      pending.bounds.second);
          continue;
        }
        SLANG_ASSERT(pending.node != nullptr &&
                     "R-value node target must not be null");
        auto &edge = addEdge(*driver, *pending.node);

        edge.setVariable(pending.symbol, pending.bounds);
      }
    }
    pendingRValues.clear();
  }

  /// @brief Merge symbol drivers from a procedural data flow analysis.
  /// @param procSymbolToSlot Mapping from symbols to slot indices.
  /// @param procDriverMap Mapping from ranges to graph nodes.
  /// @param edgeKind The kind of edge that triggers the drivers.
  auto mergeDrivers(SymbolSlotMap const &procSymbolToSlot,
                    std::vector<SymbolDriverMap> const &procDriverMap,
                    ast::EdgeKind edgeKind = ast::EdgeKind::None) -> void {

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

      if (procDriverMap.empty()) {
        // If the procedure driver map is empty, we don't need to do anything.
        continue;
      }

      // Add all the procedure driver intervals to the global map.
      for (auto it = procDriverMap[index].begin();
           it != procDriverMap[index].end(); it++) {

        // TODO
        // If this is a sequential edge...
        // Lookup a matching interval in the driver map.
        // If matching interval, add an edge from the driver node to the
        // sequential node. If no matching interval, then create a new
        // sequential node and add the interval with this node. If not
        // sequential, then just add the interval with the node.

        if (edgeKind == ast::EdgeKind::None) {
          // Combinatorial edge, just add the interval with the driving node.
          driverMap[globalIndex].insert(it.bounds(), *it, mapAllocator);
        } else {
          // Sequential edge.
          auto *driverNode = lookupDriver(*symbol, it.bounds());
          if (driverNode) {
            // If a driver node exists, add an edge from the driver node to the
            // sequential node.
            auto &edge = addEdge(*driverNode, **it);
            edge.setVariable(symbol, it.bounds());
          } else {
            // If no driver node exists, create a new sequential node and add
            // the interval with this node.
            auto &node =
                addNode(std::make_unique<NetlistNode>(NodeKind::State));
            auto &edge = addEdge(node, **it);
            edge.setVariable(symbol, it.bounds());
            driverMap[globalIndex].insert(it.bounds(), &node, mapAllocator);
          }
        }

        // Add dependencies from drivers of port symbols to the port
        // netlist node.
        if (portMap.contains(symbol) && portMap[symbol]->isOutput()) {
          auto &edge = addEdge(**it, *portMap[symbol]);
          edge.setVariable(symbol, it.bounds());
        }
      }
    }
  }

  /// Handle an L-value that is encountered during netlist construction
  /// by updating the global driver map.
  ///
  /// @param symbol The L-value symbol.
  /// @param bounds The range of the symbol that is being assigned to.
  /// @param node The netlist graph node that is the operation driving the
  /// L-value.
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

  /// Create a port node in the netlist.
  void addPort(ast::PortSymbol const &symbol) {

    // Create a node to represent the port.
    auto &node = addNode(
        std::make_unique<Port>(symbol.direction, symbol.internalSymbol));

    // Map internal symbol to a port node.
    portMap[symbol.internalSymbol] = &node.as<Port>();
  }

  /// Lookup a port netlist node by the internal symbol the port is
  /// connected to.
  [[nodiscard]] auto getPort(ast::Symbol const *symbol)
      -> std::optional<NetlistNode *> {
    if (portMap.contains(symbol)) {
      return portMap[symbol];
    }
    return std::nullopt;
  }

  /// Connect an input port by tracking that it is a driver for the internal
  /// symbol it is bound to.
  void connectInputPort(ast::ValueSymbol const &symbol,
                        std::pair<uint64_t, uint64_t> bounds) {
    handleLvalue(symbol, bounds, portMap[&symbol]);
  }

public:
  NetlistGraph() : mapAllocator(allocator) {
    NetlistNode::nextID = 0; // Reset the static ID counter.
  }

  void finalize() {
    // Process any pending R-values after the main AST traversal.
    processPendingRvalues();
  }

  /// @brief Lookup a node in the graph by its hierarchical name.
  /// @param name The hierarchical name of the node.
  /// @return A pointer to the node if found, or nullptr if not found.
  [[nodiscard]] auto lookup(std::string_view name) const -> NetlistNode * {
    auto compare = [&](const std::unique_ptr<NetlistNode> &node) {
      switch (node->kind) {
      case NodeKind::Port:
        return node->as<Port>().internalSymbol->getHierarchicalPath() == name;
      default:
        return false;
      }
    };
    auto it = std::ranges::find_if(*this, compare);
    return it != this->end() ? it->get() : nullptr;
  }
};

} // namespace slang::netlist
