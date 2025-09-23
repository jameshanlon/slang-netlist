#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"

#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/util/IntervalMap.h"

namespace slang::netlist {

/// Track information about drivers in the netlist graph.
struct DriverInfo {
  NetlistNode *node;
  const ast::Expression *lsp;
};

using SymbolSlotMap = std::map<const ast::Symbol *, uint32_t>;
using SymbolDriverMap = IntervalMap<uint64_t, DriverInfo, 8>;

struct PendingRvalue {
  not_null<const ast::ValueSymbol *> symbol;
  const ast::Expression *lsp;
  std::pair<uint64_t, uint64_t> bounds;
  NetlistNode *node{nullptr};

  PendingRvalue(const ast::ValueSymbol *symbol, const ast::Expression *lsp,
                std::pair<uint64_t, uint64_t> bounds, NetlistNode *node)
      : symbol(symbol), lsp(lsp), bounds(bounds), node(node) {}
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
                 std::pair<uint64_t, uint64_t> bounds, NetlistNode *node);

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
  auto mergeDrivers(SymbolSlotMap const &procSymbolToSlot,
                    std::vector<SymbolDriverMap> const &procDriverMap,
                    ast::EdgeKind edgeKind = ast::EdgeKind::None) -> void;

  /// Handle an L-value that is encountered during netlist construction
  /// by updating the global driver map.
  ///
  /// @param symbol The L-value symbol.
  /// @param bounds The range of the symbol that is being assigned to.
  /// @param node The netlist graph node that is the operation driving the
  /// L-value.
  void addDriver(const ast::Symbol &symbol, const ast::Expression *lsp,
                 std::pair<uint64_t, uint64_t> bounds, NetlistNode *node);

  /// Create a port node in the netlist.
  auto addPort(ast::PortSymbol const &symbol,
               std::pair<uint64_t, uint64_t> bounds) -> NetlistNode &;

  /// Create a modport node in the netlist.
  auto addModport(ast::ModportPortSymbol const &symbol,
                  std::pair<uint64_t, uint64_t> bounds) -> NetlistNode &;

  /// Lookup a node in the graph that is a driver of the specified range of the
  /// symbol.
  /// @param symbol The symbol that is being driven.
  /// @param bounds The range of the symbol that is being driven.
  /// @return A pointer to the node or null if one is not found.
  auto getFirstDriver(ast::Symbol const &symbol,
                      std::pair<uint64_t, uint64_t> bounds)
      -> std::optional<DriverInfo>;

  /// Return a list of nodes in the graph that are drivers of the specified
  /// range of the symbol.
  /// @param symbol The symbol that is being driven.
  /// @param bounds The range of the symbol that is being driven.
  /// @return A vector of driver nodes.
  std::vector<DriverInfo> getDrivers(ast::Symbol const &symbol,
                                     std::pair<uint64_t, uint64_t> bounds);
};

} // namespace slang::netlist
