#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverTracker.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"

#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/SmallVector.h"

namespace slang::netlist {

/// Information about a pending R-value that needs to be processed
/// after all drivers have been visited.
struct PendingRvalue {
  not_null<const ast::ValueSymbol *> symbol;
  const ast::Expression *lsp;
  DriverBitRange bounds;
  NetlistNode *node{nullptr};

  PendingRvalue(const ast::ValueSymbol *symbol, const ast::Expression *lsp,
                DriverBitRange bounds, NetlistNode *node)
      : symbol(symbol), lsp(lsp), bounds(bounds), node(node) {}
};

/// Represent the netlist connectivity of an elaborated design.
class NetlistGraph : public DirectedGraph<NetlistNode, NetlistEdge> {

  friend class NetlistVisitor;
  friend class DataFlowAnalysis;

  // Symbol to bit ranges mapping to the netlist node(s) that are driving them.
  DriverTracker driverMap;

  SymbolDrivers drivers;

  // Pending R-values that need to be connected after the main AST traversal.
  std::vector<PendingRvalue> pendingRValues;

public:
  NetlistGraph();

  /// Finalize the netlist graph after construction is complete.
  void finalize();

  /// Lookup a node in the graph by its hierarchical name.
  ///
  /// @param name The hierarchical name of the node.
  /// @return A pointer to the node if found, or nullptr if not found.
  [[nodiscard]] auto lookup(std::string_view name) const -> NetlistNode *;

private:
  /// Add an R-value to a pending list to be processed once all drivers have
  /// been visited.
  void addRvalue(ast::ValueSymbol const &symbol, ast::Expression const &lsp,
                 DriverBitRange bounds, NetlistNode *node);

  /// Process pending R-values after the main AST traversal.
  ///
  /// This connects the pending R-values to their respective nodes in the
  /// netlist graph. This is necessary to ensure that all drivers are processed
  /// before handling R-values, as they may depend on the drivers being present
  /// in the graph. This method should be called after the main AST traversal is
  /// complete.
  void processPendingRvalues();

  /// Merge symbol drivers from a procedural data flow analysis.
  ///
  /// @param procSymbolToSlot Mapping from symbols to slot indices.
  /// @param procDriverMap Mapping from ranges to graph nodes.
  /// @param edgeKind The kind of edge that triggers the drivers.
  // auto mergeDrivers(SymbolSlotMap const &procSymbolToSlot,
  //                   std::vector<DriverMap> const &procDriverMap,
  //                   ast::EdgeKind edgeKind = ast::EdgeKind::None) -> void;

  /// Add a driver for the specified symbol.
  /// This overwrites any existing drivers for the specified bit range.
  auto addDriver(ast::Symbol const &symbol, ast::Expression const *lsp,
                 DriverBitRange bounds, NetlistNode *node) -> void {
    driverMap.addDriver(drivers, symbol, lsp, bounds, node);
  }

  /// Get a list of all the drivers for the given symbol and bit range.
  /// If there are no drivers, the returned list will be empty.
  auto getDrivers(ast::Symbol const &symbol, DriverBitRange bounds)
      -> DriverList {
    return driverMap.getDrivers(drivers, symbol, bounds);
  }

  /// Create a port node in the netlist.
  auto addPort(ast::PortSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode &;

  /// Create a modport node in the netlist.
  auto addModport(ast::ModportPortSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode &;
};

} // namespace slang::netlist
