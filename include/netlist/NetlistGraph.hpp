#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/LSPUtilities.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Expression.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/InstanceSymbols.h"
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

  // Map interface instance + member name to netlist node that is driving the
  // member.
  std::map<std::pair<ast::InstanceSymbol const *, std::string>, Variable *>
      interfaceMap;

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
  void processPendingRvalues(analysis::AnalysisManager &analysisManager) {
    for (auto &pending : pendingRValues) {
      DEBUG_PRINT("Processing pending R-value: {} [{}:{}]\n",
                  pending.symbol->getHierarchicalPath(), pending.bounds.first,
                  pending.bounds.second);

      /// If no driver is found from previous analysis, then check Slang's
      /// driver / tracker.
      // auto drivers = analysisManager.getDrivers(*pending.symbol);
      // for (auto &[driver, bounds] : drivers) {
      //   DEBUG_PRINT(
      //       "  Driven by {} [{}:{}] prefix={}\n", toString(driver->kind),
      //       bounds.first, bounds.second,
      //       netlist::LSPUtilities::getLSPName(*pending.symbol, *driver));
      // }

      if (pending.node) {

        // Find drivers of the pending R-value, and for each one add edges from
        // the driver to the R-value.
        if (symbolToSlot.contains(pending.symbol)) {
          auto &map = driverMap[symbolToSlot[pending.symbol]];
          for (auto it = map.find(pending.bounds); it != map.end(); it++) {
            auto &edge = addEdge(**it, *pending.node);
            edge.setVariable(pending.symbol, pending.bounds);
            DEBUG_PRINT("  Added edge from driver node {} to R-value node {}\n",
                        (*it)->ID, pending.node->ID);
          }
        }
      }
    }
    pendingRValues.clear();
  }

  /// Merge symbol drivers from a procedural data flow analysis.
  ///
  /// @param analysisManager The analysis manager to use for driver lookups.
  /// @param procSymbolToSlot Mapping from symbols to slot indices.
  /// @param procDriverMap Mapping from ranges to graph nodes.
  /// @param edgeKind The kind of edge that triggers the drivers.
  auto mergeDrivers(SymbolSlotMap const &procSymbolToSlot,
                    std::vector<SymbolDriverMap> const &procDriverMap,
                    ast::EdgeKind edgeKind = ast::EdgeKind::None) -> void {

    for (auto [symbol, index] : procSymbolToSlot) {
      DEBUG_PRINT("Merging drivers for symbol {} at proc index {}\n",
                  symbol->getHierarchicalPath(), index);

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
        DEBUG_PRINT("  Merging driver interval: [{}:{}]\n", it.bounds().first,
                    it.bounds().second);

        NetlistNode *node = nullptr;
        if (edgeKind == ast::EdgeKind::None) {
          // Combinatorial edge, just add the interval with the driving node.
          driverMap[globalIndex].insert(it.bounds(), *it, mapAllocator);
          node = *it;
        } else {
          // Sequential edge.
          node = lookupDriver(*symbol, it.bounds());
          if (node) {
            // If a driver node exists, add an edge from the driver node to the
            // sequential node.
            addEdge(**it, *node).setVariable(symbol, it.bounds());
          } else {
            // If no driver node exists, create a new sequential node and add
            // the interval with this node.
            node = &addNode(std::make_unique<State>(symbol, it.bounds()));
            addEdge(**it, *node).setVariable(symbol, it.bounds());
            driverMap[globalIndex].insert(it.bounds(), node, mapAllocator);
          }
        }

        // If there is an output port associated with this symbol, then add a
        // dependency from the driver to the port.
        if (portMap.contains(symbol) && portMap[symbol]->isOutput()) {
          DEBUG_PRINT("Adding port dependency for symbol {} to port {}\n",
                      symbol->name, portMap[symbol]->internalSymbol->name);
          auto &edge = addEdge(*node, *portMap[symbol]);
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
    DEBUG_PRINT("Handle global lvalue: {} [{}:{}]\n",
                symbol.getHierarchicalPath(), bounds.first, bounds.second);

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

    DEBUG_PRINT("Added port {} (internal {}) dir={}\n", symbol.name,
                symbol.internalSymbol ? symbol.internalSymbol->name : "null",
                toString(symbol.direction));
  }

  /// Create a variable node in the netlist to hook up interface connections
  /// via.
  void addInterfaceVariable(ast::InstanceSymbol const &instance,
                            ast::VariableSymbol const &variable) {

    // Create a node to represent the variable.
    auto &node = addNode(std::make_unique<Variable>(&variable));

    // Record mapping from interface to nodes for its members.
    interfaceMap.insert(
        std::pair{std::pair{&instance, variable.name}, &node.as<Variable>()});

    DEBUG_PRINT("Added interface variable {}.{}\n", instance.name,
                variable.name);
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

  void finalize(analysis::AnalysisManager &analysisManager) {
    // Process any pending R-values after the main AST traversal.
    processPendingRvalues(analysisManager);
  }

  /// Lookup a node in the graph by its hierarchical name.
  ///
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
