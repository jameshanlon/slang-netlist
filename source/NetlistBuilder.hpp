#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <numeric>
#include <vector>

#include <BS_thread_pool.hpp>

#include "BitSliceList.hpp"
#include "BuildPipeline.hpp"
#include "CanonicalBodyResolver.hpp"
#include "NodeFactory.hpp"
#include "PendingRvalueQueue.hpp"
#include "PortConnectionHandler.hpp"
#include "ValueTracker.hpp"
#include "VariableTracker.hpp"

#include "netlist/BuildProfile.hpp"
#include "netlist/BuilderOptions.hpp"
#include "netlist/Debug.hpp"
#include "netlist/NetlistGraph.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/util/FlatMap.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/SmallVector.h"

namespace slang::netlist {

/// A class that manages construction of the netlist graph.
class NetlistBuilder
    : public ast::ASTVisitor<NetlistBuilder, ast::VisitFlags::Expressions |
                                                 ast::VisitFlags::Canonical> {

  friend class DataFlowAnalysis;

  ast::Compilation &compilation;
  analysis::AnalysisManager &analysisManager;

  // The netlist graph itself.
  NetlistGraph &graph;

  // Symbol to bit ranges, mapping to the netlist node(s) that are driving
  // them.
  ValueTracker driverMap;

  // Driver maps for each symbol.
  ValueDrivers drivers;

  // Track netlist nodes that represent ranges of variables.
  VariableTracker variables;

  /// Caller-supplied build options.
  BuilderOptions options;

  /// Resolves AST symbols to their canonical counterparts so driver
  /// queries against slang's AnalysisManager redirect correctly.
  CanonicalBodyResolver canonicalResolver;

  /// Allocates netlist nodes and registers them with the graph and
  /// the variable tracker.
  NodeFactory nodeFactory{*this};

  /// Owns the bit-aligned port-connection state (slice allocator and
  /// cut registry) and dispatches port wiring.
  PortConnectionHandler portHandler{*this};

  /// Owns the pending-rvalue queue and resolves it into edges in
  /// Phase 4.
  PendingRvalueQueue pendingQueue{*this};

  /// Orchestrator for the four build phases.
  BuildPipeline pipeline{*this};

  friend class NodeFactory;
  friend class PortConnectionHandler;
  friend class PendingRvalueQueue;
  friend class BuildPipeline;

public:
  NetlistBuilder(ast::Compilation &compilation,
                 analysis::AnalysisManager &analysisManager,
                 NetlistGraph &graph, BuilderOptions options = {});

  /// Convert a slang SourceLocation to a TextLocation using the
  /// compilation's SourceManager and the graph's FileTable.
  auto toTextLocation(SourceLocation loc) const -> TextLocation;

  /// Extract a SymbolReference from a live AST symbol.
  auto toSymbolRef(ast::Symbol const &sym) const -> SymbolReference;

  /// Build the netlist graph from the given root symbol using a two-phase
  /// collect-then-dispatch approach. Phase 1 visits the AST sequentially to
  /// create ports, variables, and instance structure. Phase 2 dispatches
  /// deferred DFA work items in parallel when `options.parallel` is true.
  /// `options.numThreads` specifies the thread pool size; 0 means use
  /// hardware concurrency.
  void build(const ast::Symbol &root);

  /// Finalize the netlist graph after construction is complete.
  void finalize();

  /// Return the profiling data collected during build().
  auto getBuildProfile() const -> BuildProfile const & {
    return pipeline.getProfile();
  }

  void handle(ast::PortSymbol const &symbol);
  void handle(ast::VariableSymbol const &symbol);
  void handle(ast::InstanceSymbol const &symbol);

  /// Whether @p symbol should be treated as a black box: its body is
  /// not visited, so no internal nodes or edges are created. Matched
  /// against `options.blackBoxes` by either definition name (every
  /// instance of the named module) or hierarchical instance path
  /// (one specific instance).
  bool isBlackBoxInstance(ast::InstanceSymbol const &symbol) const;
  void handle(ast::ProceduralBlockSymbol const &symbol);
  void handle(ast::ContinuousAssignSymbol const &symbol);
  void handle(ast::GenerateBlockSymbol const &symbol);

private:
  /// Helper function to visit members of a symbol.
  template <typename T> void visitMembers(const T &symbol) {
    for (auto &member : symbol.members()) {
      member.visit(*this);
    }
  }

  /// Clear the per-thread symbol-ref cache. Called at parallel-task
  /// boundaries so stale entries from a prior task can't leak.
  void clearThreadLocalSymbolRefCache();

  /// Execute the DFA for a procedural block.
  void handleProceduralBlock(ast::ProceduralBlockSymbol const &symbol);

  /// Execute the DFA for a continuous assignment.
  void handleContinuousAssign(ast::ContinuousAssignSymbol const &symbol);

  /// Return a string representation of a driver's LSP.
  static auto getDriverPathName(ast::ValueSymbol const &symbol,
                                analysis::ValueDriver const &driver)
      -> std::string;

  /// One event-list entry: signal symbol, its LSP expression, and edge
  /// kind. Non-symbol expressions (bit-selects, arithmetic) are dropped.
  struct SensitivityEntry {
    ast::ValueSymbol const *signal;
    ast::Expression const *lsp;
    ast::EdgeKind edgeKind;
  };

  /// Per-signal sensitivity from a procedural block's event list. Empty
  /// when the block is combinational (no timing control or any unqualified
  /// event).
  static auto collectSensitivity(ast::ProceduralBlockSymbol const &symbol)
      -> SmallVector<SensitivityEntry>;

  auto getVariable(ast::Symbol const &symbol, DriverBitRange bounds)
      -> NetlistNode * {
    return variables.lookup(symbol, bounds);
  }

  auto getVariable(ast::Symbol const &symbol) -> std::vector<NetlistNode *> {
    return variables.lookup(symbol);
  }

  /// Add a dependency between two nodes in the netlist.
  void addDependency(NetlistNode &source, NetlistNode &target);

  /// Add a dependency between two nodes in the netlist.
  /// Specify the symbol and bounds that are being driven to annotate the edge.
  void addDependency(NetlistNode &source, NetlistNode &target,
                     SymbolReference symbol, DriverBitRange bounds,
                     ast::EdgeKind edgeKind = ast::EdgeKind::None);

  /// Add a list of drivers to the target node. Annotate the edges with the
  /// driven symbol and its bounds.
  void addDriversToNode(DriverList const &drivers, NetlistNode &node,
                        SymbolReference symbol, DriverBitRange bounds);

  /// Merge two nodes by creating a new merge node, creating dependencies from
  /// them to the merge and return a reference to the merge node.
  auto merge(NetlistNode &a, NetlistNode &b) -> NetlistNode &;

  struct InterfaceVarBounds {
    ast::VariableSymbol const &symbol;
    DriverBitRange bounds;
  };

  /// Helper method for resolving a modport port symbol LSP to interface
  /// variables and their bounds.
  void _resolveInterfaceRef(BumpAllocator &alloc,
                            std::vector<InterfaceVarBounds> &result,
                            ast::EvalContext &evalCtx,
                            ast::ModportPortSymbol const &symbol,
                            ast::Expression const &prefixExpr);

  /// Given a modport port symbol LSP, return a list of interface symbols and
  /// their bounds that the value resolves to.
  auto resolveInterfaceRef(ast::EvalContext &evalCtx,
                           ast::ModportPortSymbol const &symbol,
                           ast::Expression const &lsp)
      -> std::vector<InterfaceVarBounds>;

  /// Add an R-value to a pending list to be processed once all drivers have
  /// been visited. Modport rvalues are resolved synchronously; everything
  /// else is enqueued onto `pendingQueue` for Phase 4 resolution.
  void addRvalue(ast::EvalContext &evalCtx, ast::ValueSymbol const &symbol,
                 ast::Expression const &lsp, DriverBitRange bounds,
                 NetlistNode *node);

  /// If the specified symbol has an output port back reference, then connect
  /// the drivers to the port node. This is called when merging driver into
  /// the graph.
  void hookupOutputPort(ast::ValueSymbol const &symbol, DriverBitRange bounds,
                        DriverList const &driverList);

  /// Add a driver for the specified symbol.
  /// This overwrites any existing drivers for the specified bit range.
  auto addDriver(ast::ValueSymbol const &symbol, ast::Expression const *lsp,
                 DriverBitRange bounds, NetlistNode *node) -> void {
    driverMap.addDrivers(drivers, symbol, bounds, {DriverInfo(node, lsp)});
  }

  /// Merge a list of drivers for the specified symbol and bit range into the
  /// central driver tracker.
  auto mergeDrivers(ast::ValueSymbol const &symbol, DriverBitRange bounds,
                    DriverList const &driverList) -> void {
    driverMap.addDrivers(drivers, symbol, bounds, driverList, /*merge=*/true);
  }

  /// Merge procedural drivers into the central tracker. Non-empty
  /// @p sensitivity makes the block sequential: each driven range gets a
  /// State node, with data edges from drivers and a clock edge per
  /// sensitivity entry.
  void mergeDrivers(ast::EvalContext &evalCtx, ValueTracker const &valueTracker,
                    ValueDrivers const &valueDrivers,
                    std::span<SensitivityEntry const> sensitivity = {});
};

} // namespace slang::netlist
