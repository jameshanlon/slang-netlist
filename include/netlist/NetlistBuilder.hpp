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
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/SmallVector.h"

namespace slang::netlist {

/// Information about a pending rvalue that needs to be processed
/// after all drivers have been visited.
struct PendingRvalue {

  // Identify the rvalue.
  not_null<const ast::ValueSymbol *> symbol;
  DriverBitRange bounds;

  // The LSP of the rvalue.
  const ast::Expression *lsp;

  // The operation in which the rvalue appears.
  NetlistNode *node{nullptr};

  PendingRvalue(const ast::ValueSymbol *symbol, const ast::Expression *lsp,
                DriverBitRange bounds, NetlistNode *node)
      : symbol(symbol), lsp(lsp), bounds(bounds), node(node) {}
};

struct VariableTracker {

  using VariableMap = IntervalMap<uint32_t, NetlistNode *>;
  BumpAllocator ba;
  VariableMap::allocator_type alloc;
  std::map<ast::Symbol const *, VariableMap> variables;

  VariableTracker() : alloc(ba) {}

  auto insert(ast::Symbol const &symbol, DriverBitRange bounds,
              NetlistNode &node) {
    SLANG_ASSERT(!variables.contains(&symbol));
    variables.emplace(&symbol, VariableMap());
    variables[&symbol].insert(bounds, &node, alloc);
  }

  auto lookup(ast::Symbol const &symbol, DriverBitRange bounds) const
      -> NetlistNode * {
    if (variables.contains(&symbol)) {
      auto &map = variables.find(&symbol)->second;
      for (auto it = map.find(bounds); it != map.end(); it++) {
        if (it.bounds() == bounds) {
          return *it;
        }
      }
    }
    return nullptr;
  }
};

/// A class that manages construction of the netlist graph.
class NetlistBuilder {

  friend class NetlistVisitor;
  friend class DataFlowAnalysis;

  ast::Compilation &compilation;

  // The netlist graph itself.
  NetlistGraph &graph;

  // Symbol to bit ranges, mapping to the netlist node(s) that are driving
  // them.
  SymbolTracker driverMap;

  // Driver maps for each symbol.
  SymbolDrivers drivers;

  // Track netlist nodes that represent ranges of variables.
  VariableTracker variables;

  // Pending R-values that need to be connected after the main AST traversal.
  std::vector<PendingRvalue> pendingRValues;

public:
  NetlistBuilder(ast::Compilation &compilation, NetlistGraph &graph);

  /// Finalize the netlist graph after construction is complete.
  void finalize();

private:
  /// Create a port node in the netlist.
  auto createPort(ast::PortSymbol const &symbol) -> NetlistNode & {
    return graph.addNode(std::make_unique<Port>(symbol));
  }

  /// Create a variable node in the netlist.
  auto createVariable(ast::VariableSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode & {
    auto &node = graph.addNode(std::make_unique<Variable>(symbol));
    variables.insert(symbol, bounds, node);
    return node;
  }

  auto getVariable(ast::VariableSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode * {
    return variables.lookup(symbol, bounds);
  }

  /// Create an assignment node in the netlist.
  auto createAssignment(ast::AssignmentExpression const &expr)
      -> NetlistNode & {
    return graph.addNode(std::make_unique<Assignment>(expr));
  }

  /// Create a conditional node in the netlist.
  auto createConditional(ast::ConditionalStatement const &stmt)
      -> NetlistNode & {
    return graph.addNode(std::make_unique<Conditional>(stmt));
  }

  /// Create a case node in the netlist.
  auto createCase(ast::CaseStatement const &stmt) -> NetlistNode & {
    return graph.addNode(std::make_unique<Case>(stmt));
  }

  /// Add a dependency between two nodes in the netlist.
  auto addDependency(NetlistNode &from, NetlistNode &to) -> NetlistEdge & {
    return graph.addEdge(from, to);
  }

  /// Add a list of drivers to the target node. Annotate the edges with the
  /// driven symbol and its bounds.
  void addDriversToNode(DriverList const &drivers, NetlistNode &node,
                        ast::Symbol const &symbol, DriverBitRange bounds) {
    for (auto driver : drivers) {
      if (driver.node) {
        addDependency(*driver.node, node).setVariable(&symbol, bounds);
      }
    }
  }

  /// Merge two nodes by creating a new merge node, creating dependencies from
  /// them to the merge and return a reference to the merge node.
  auto merge(NetlistNode &a, NetlistNode &b) -> NetlistNode & {
    if (a.ID == b.ID) {
      return a;
    }

    auto &node = graph.addNode(std::make_unique<Merge>());
    addDependency(a, node);
    addDependency(b, node);
    return node;
  }

  struct InterfaceVarBounds {
    ast::VariableSymbol const &symbol;
    DriverBitRange bounds;
  };

  /// TODO
  void _resolveInterfaceRef(BumpAllocator &alloc,
                            std::vector<InterfaceVarBounds> &result,
                            ast::EvalContext &evalCtx,
                            ast::ModportPortSymbol const &symbol,
                            ast::Expression const &lsp);

  /// TODO Given a modport port symbol and it's LSP expression, ...
  auto resolveInterfaceRef(ast::EvalContext &evalCtx,
                           ast::ModportPortSymbol const &symbol,
                           ast::Expression const &lsp)
      -> std::vector<InterfaceVarBounds>;

  /// Add an R-value to a pending list to be processed once all drivers have
  /// been visited.
  void addRvalue(ast::EvalContext &evalCtx, ast::ValueSymbol const &symbol,
                 ast::Expression const &lsp, DriverBitRange bounds,
                 NetlistNode *node);

  /// Process pending R-values after the main AST traversal.
  ///
  /// This connects the pending R-values to their respective nodes in the
  /// netlist graph. This is necessary to ensure that all drivers are
  /// processed before handling R-values, as they may depend on the drivers
  /// being present in the graph. This method should be called after the main
  /// AST traversal is complete.
  void processPendingRvalues();

  /// If the specified symbol has an output port back reference, then connect
  /// the drivers to the port node. This is called when merging driver into
  /// the graph.
  void hookupOutputPort(ast::ValueSymbol const &symbol, DriverBitRange bounds,
                        DriverList const &driverList);

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

  /// Merge a list of drivers for the specified symbol and bit range into the
  /// central driver tracker.
  auto mergeDrivers(ast::Symbol const &symbol, DriverBitRange bounds,
                    DriverList const &driverList) -> void {
    driverMap.mergeDrivers(drivers, symbol, bounds, driverList);
  }

  /// Merge symbol drivers from a procedural data flow analysis into the
  /// central driver tracker.
  void mergeProcDrivers(ast::EvalContext &evalCtx,
                        SymbolTracker const &symbolTracker,
                        SymbolDrivers const &symbolDrivers,
                        ast::EdgeKind edgeKind = ast::EdgeKind::None);

  /// Get a list of all the drivers for the given symbol and bit range.
  /// If there are no drivers, the returned list will be empty.
  auto getDrivers(ast::Symbol const &symbol, DriverBitRange bounds)
      -> DriverList {
    return driverMap.getDrivers(drivers, symbol, bounds);
  }
};

} // namespace slang::netlist
