#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "netlist/Debug.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/PendingRValue.hpp"
#include "netlist/ValueTracker.hpp"
#include "netlist/VariableTracker.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/SmallVector.h"

namespace slang::netlist {

/// Thread-local accumulator for graph mutations during parallel Phase 2.
/// Each parallel task gets its own buffer; after all tasks complete the
/// buffers are drained into the shared graph sequentially.
struct DeferredGraphWork {
  struct DeferredEdge {
    NetlistNode *source;
    NetlistNode *target;
    SymbolReference symbol;
    DriverBitRange bounds;
    ast::EdgeKind edgeKind;
  };

  std::vector<std::unique_ptr<NetlistNode>> nodes;
  std::vector<DeferredEdge> edges;
  std::vector<PendingRvalue> pendingRValues;
  std::vector<std::function<void()>> deferredMerges;

  auto addNode(std::unique_ptr<NetlistNode> node) -> NetlistNode & {
    nodes.push_back(std::move(node));
    return *nodes.back();
  }
};

/// Visitor to visit the entire AST prior to freezing. This is required since
/// AST construction is lazy, so visiting a previously univisted node can cause
/// modifications, which is not threadsafe.  This allows the subsequent netlist
/// construction pass to be multithreaded, in the same way Slang's analysis pass
/// is.
struct VisitAll : public ast::ASTVisitor<VisitAll,
                                         /*VisitStatements=*/true,
                                         /*VisitExpressions=*/true,
                                         /*VisitBad=*/false,
                                         /*VisitCanonical=*/false> {
  uint64_t count;

  void handle(const ast::ValueSymbol &symbol) { count++; }
};

/// A class that manages construction of the netlist graph.
class NetlistBuilder : public ast::ASTVisitor<NetlistBuilder,
                                              /*VisitStatements=*/false,
                                              /*VisitExpressions=*/true,
                                              /*VisitBad=*/false,
                                              /*VisitCanonical=*/true> {

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

  // Pending R-values that need to be connected after the main AST traversal.
  std::vector<PendingRvalue> pendingRValues;

  /// A deferred procedural or continuous assignment block for parallel
  /// dispatch.
  struct DeferredBlock {
    const ast::Symbol *symbol;
    bool isProcedural; // true = ProceduralBlock, false = ContinuousAssign
  };

  /// Work list of deferred blocks collected during Phase 1.
  std::vector<DeferredBlock> deferredBlocks;

  /// When true, procedural/continuous blocks are collected rather than
  /// executed.
  bool collectingPhase = false;

public:
  NetlistBuilder(ast::Compilation &compilation,
                 analysis::AnalysisManager &analysisManager,
                 NetlistGraph &graph);

  /// Build the netlist graph from the given root symbol using a two-phase
  /// collect-then-dispatch approach. Phase 1 visits the AST sequentially to
  /// create ports, variables, and instance structure. Phase 2 dispatches
  /// deferred DFA work items in parallel (when parallel=true and threads are
  /// available). \p numThreads specifies the thread pool size; 0 means use
  /// hardware concurrency.
  void build(const ast::Symbol &root, bool parallel = true,
             unsigned numThreads = 0);

  /// Finalize the netlist graph after construction is complete.
  void finalize();

  void handle(ast::PortSymbol const &symbol);
  void handle(ast::VariableSymbol const &symbol);
  void handle(ast::InstanceSymbol const &symbol);
  void handle(ast::ProceduralBlockSymbol const &symbol);
  void handle(ast::ContinuousAssignSymbol const &symbol);
  void handle(ast::GenerateBlockSymbol const &symbol);

  /// Get a list of all the drivers for the given symbol and bit range.
  /// If there are no drivers, the returned list will be empty.
  auto getDrivers(ast::ValueSymbol const &symbol, DriverBitRange bounds) const
      -> DriverList {
    return driverMap.getDrivers(drivers, symbol, bounds);
  }

private:
  /// Helper function to visit members of a symbol.
  template <typename T> void visitMembers(const T &symbol) {
    for (auto &member : symbol.members()) {
      member.visit(*this);
    }
  }

  /// Drain deferred graph work buffers into the shared graph sequentially.
  void drainDeferredWork(std::vector<DeferredGraphWork> &allWork);

  /// Execute the DFA for a procedural block.
  void handleProceduralBlock(ast::ProceduralBlockSymbol const &symbol);

  /// Execute the DFA for a continuous assignment.
  void handleContinuousAssign(ast::ContinuousAssignSymbol const &symbol);

  /// Return a string representation of an LSP.
  static auto getLSPName(ast::ValueSymbol const &symbol,
                         analysis::ValueDriver const &driver) -> std::string;

  /// Determine the edge type to apply within a procedural
  /// block.
  static auto determineEdgeKind(ast::ProceduralBlockSymbol const &symbol)
      -> ast::EdgeKind;

  /// Create a port node in the netlist.
  auto createPort(ast::PortSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode &;

  /// Create a variable node in the netlist.
  auto createVariable(ast::VariableSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode &;

  auto getVariable(ast::Symbol const &symbol, DriverBitRange bounds)
      -> NetlistNode * {
    return variables.lookup(symbol, bounds);
  }

  auto getVariable(ast::Symbol const &symbol) -> std::vector<NetlistNode *> {
    return variables.lookup(symbol);
  }

  /// Create a state node in the netlist.
  auto createState(ast::ValueSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode &;

  /// Create an assignment node in the netlist.
  auto createAssignment(ast::AssignmentExpression const &expr) -> NetlistNode &;

  /// Create a conditional node in the netlist.
  auto createConditional(ast::ConditionalStatement const &stmt)
      -> NetlistNode &;

  /// Create a case node in the netlist.
  auto createCase(ast::CaseStatement const &stmt) -> NetlistNode &;

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
                        DriverList const &driverList,
                        ast::EdgeKind edgeKind = ast::EdgeKind::None);

  void handlePortConnection(ast::Symbol const &containingSymbol,
                            ast::PortConnection const &portConnection);

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

  /// Merge symbol drivers from a procedural data flow analysis into the
  /// central driver tracker.
  void mergeDrivers(ast::EvalContext &evalCtx, ValueTracker const &valueTracker,
                    ValueDrivers const &valueDrivers,
                    ast::EdgeKind edgeKind = ast::EdgeKind::None);
};

} // namespace slang::netlist
