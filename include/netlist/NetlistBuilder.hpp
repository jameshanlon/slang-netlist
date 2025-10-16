#pragma once

#include "netlist/Debug.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/SymbolTracker.hpp"

#include "slang/ast/Compilation.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/SmallVector.h"

namespace slang::netlist {

/// Information about a pending rvalue that needs to be processed
/// after all drivers have been visited. The members `symbol` and `lsp`
/// identify the R-value, `bounds` gives the bit range, and `node` is
/// the operation in which the rvalue appears.
struct PendingRvalue {
  not_null<const ast::ValueSymbol *> symbol;
  const ast::Expression *lsp;
  DriverBitRange bounds;
  NetlistNode *node{nullptr};

  PendingRvalue(const ast::ValueSymbol *symbol, const ast::Expression *lsp,
                DriverBitRange bounds, NetlistNode *node)
      : symbol(symbol), lsp(lsp), bounds(bounds), node(node) {}
};

/// A class that manages construction of the netlist graph.
class NetlistBuilder {

  friend class NetlistVisitor;
  friend class DataFlowAnalysis;

  ast::Compilation &compilation;

  // The netlist graph itself.
  NetlistGraph &graph;

  // Symbol to bit ranges mapping to the netlist node(s) that are driving them.
  SymbolTracker driverMap;

  // Driver maps for each symbol.
  SymbolDrivers drivers;

  // Pending R-values that need to be connected after the main AST traversal.
  std::vector<PendingRvalue> pendingRValues;

public:
  NetlistBuilder(ast::Compilation &compilation, NetlistGraph &graph);

  /// Finalize the netlist graph after construction is complete.
  void finalize();

private:
  auto createAssignment(ast::AssignmentExpression const &expr)
      -> NetlistNode & {
    return graph.addNode(std::make_unique<Assignment>(expr));
  }

  auto createConditional(ast::ConditionalStatement const &stmt)
      -> NetlistNode & {
    return graph.addNode(std::make_unique<Conditional>(stmt));
  }

  auto createCase(ast::CaseStatement const &stmt) -> NetlistNode & {
    return graph.addNode(std::make_unique<Case>(stmt));
  }

  auto addDependency(NetlistNode &from, NetlistNode &to) -> NetlistEdge & {
    return graph.addEdge(from, to);
  }

  void addDriverToOutputPort(NetlistNode &driver, NetlistNode &target) {
    graph.addEdge(driver, target);
  }

  void addDriversToNode(ast::Symbol const &symbol, DriverList const &drivers,
                        NetlistNode &node, DriverBitRange bounds) {
    for (auto driver : drivers) {
      if (driver.node) {
        auto &edge = graph.addEdge(*driver.node, node);
        edge.setVariable(&symbol, bounds);
      }
    }
  }

  auto merge(NetlistNode &a, NetlistNode &b) -> NetlistNode & {
    if (a.ID == b.ID) {
      return a;
    }

    auto &node = graph.addNode(std::make_unique<Merge>());
    graph.addEdge(a, node);
    graph.addEdge(b, node);
    return node;
  }

  void resolveInterfaceReferences(ast::EvalContext &evalCtx,
                                  ast::ValueSymbol const &symbol,
                                  ast::Expression const &lsp);

  /// Add an R-value to a pending list to be processed once all drivers have
  /// been visited.
  void addRvalue(ast::EvalContext &evalCtx, ast::ValueSymbol const &symbol,
                 ast::Expression const &lsp, DriverBitRange bounds,
                 NetlistNode *node);

  /// Process pending R-values after the main AST traversal.
  ///
  /// This connects the pending R-values to their respective nodes in the
  /// netlist graph. This is necessary to ensure that all drivers are processed
  /// before handling R-values, as they may depend on the drivers being present
  /// in the graph. This method should be called after the main AST traversal is
  /// complete.
  void processPendingRvalues();

  /// If the specified symbol has an output port back reference, then connect
  /// the drivers to the port node. This is called when merging driver into the
  /// graph.
  void hookupOutputPort(ast::ValueSymbol const &symbol, DriverBitRange bounds,
                        DriverList const &driverList);

  /// Merge symbol drivers from a procedural data flow analysis.
  void mergeProcDrivers(ast::EvalContext &evalCtx,
                        SymbolTracker const &symbolTracker,
                        SymbolDrivers const &symbolDrivers,
                        ast::EdgeKind edgeKind = ast::EdgeKind::None);

  /// Add a driver for the specified symbol.
  /// This overwrites any existing drivers for the specified bit range.
  auto addDriver(ast::Symbol const &symbol, ast::Expression const *lsp,
                 DriverBitRange bounds, NetlistNode *node) -> void {
    driverMap.addDriver(drivers, symbol, lsp, bounds, node);
  }

  /// Merge a driver for the specified symbol.
  /// This adds to any existing drivers for the specified bit range.
  auto mergeDriver(ast::Symbol const &symbol, ast::Expression const *lsp,
                   DriverBitRange bounds, NetlistNode *node) -> void {
    driverMap.mergeDriver(drivers, symbol, lsp, bounds, node);
  }

  /// Merge a list of drivers for the specified symbol and bit range.
  auto mergeDrivers(ast::Symbol const &symbol, DriverBitRange bounds,
                    DriverList const &driverList) -> void {
    driverMap.mergeDrivers(drivers, symbol, bounds, driverList);
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
};

} // namespace slang::netlist
