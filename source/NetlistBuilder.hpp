#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <numeric>
#include <vector>

#include <BS_thread_pool.hpp>

#include "BitSliceList.hpp"
#include "CutRegistry.hpp"
#include "PendingRValue.hpp"
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

/// Thread-local accumulator for deferred work during parallel Phase 2.
/// Only pending R-values are collected here; nodes, edges, and
/// mergeDrivers calls go directly to the shared graph.
struct DeferredGraphWork {
  std::vector<PendingRvalue> pendingRValues;
  double elapsedSeconds = 0; // Wall-clock time for this task.
};

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

  /// Profiling data accumulated during build().
  BuildProfile profile;

  /// Thread pool shared between Phase 2 and Phase 4.
  /// Created in build() when parallel=true, destroyed in finalize().
  std::unique_ptr<BS::thread_pool<>> threadPool;

  /// Whether parallel mode is enabled (set by build()).
  bool parallelExecution = false;

  /// Caller-supplied build options.
  BuilderOptions options;

  /// Allocator reused across BitSliceList::build() invocations on the
  /// port-connection path.
  BumpAllocator sliceAllocator;

  /// Cut hints propagated from concat-shaped port-connection actuals
  /// to the formal ports' internal symbols. Drives port-node and
  /// internal-assignment splitting.
  CutRegistry cutRegistry;

  /// Memoized mapping from a value symbol in a non-canonical instance body
  /// to the equivalent symbol in the canonical body, where slang's analysis
  /// manager actually stored the drivers. A self-mapping means "no
  /// redirection needed".
  flat_hash_map<ast::ValueSymbol const *, ast::ValueSymbol const *>
      canonicalValueCache;

public:
  /// Minimum number of pending R-values required before Phase 4 uses the
  /// parallel resolution path.
  size_t parallelRValueThreshold = 1000;

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
  /// deferred DFA work items in parallel (when parallel=true and threads are
  /// available). \p numThreads specifies the thread pool size; 0 means use
  /// hardware concurrency.
  void build(const ast::Symbol &root, bool parallel = true,
             unsigned numThreads = 0);

  /// Finalize the netlist graph after construction is complete.
  void finalize();

  /// Return the profiling data collected during build().
  auto getBuildProfile() const -> BuildProfile const & { return profile; }

  void handle(ast::PortSymbol const &symbol);
  void handle(ast::VariableSymbol const &symbol);
  void handle(ast::InstanceSymbol const &symbol);
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

  /// Drain deferred graph work buffers into the shared graph sequentially.
  void drainDeferredWork(std::vector<DeferredGraphWork> &allWork);

  /// Execute the DFA for a procedural block.
  void handleProceduralBlock(ast::ProceduralBlockSymbol const &symbol);

  /// Execute the DFA for a continuous assignment.
  void handleContinuousAssign(ast::ContinuousAssignSymbol const &symbol);

  /// Return a string representation of a driver's LSP.
  static auto getDriverPathName(ast::ValueSymbol const &symbol,
                                analysis::ValueDriver const &driver)
      -> std::string;

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

  /// Create a constant-driver node in the netlist.
  auto createConstant(ConstantValue value, uint64_t width,
                      TextLocation location) -> NetlistNode &;

  /// Materialize a Constant node for the bits a
  /// `BitSliceSource::Kind::Constant` source contributes to one aligned
  /// segment. Slices @p src's value down to the segment's bit range when wider,
  /// derives the node location from the source's recorded expression (falling
  /// back to @p fallbackLoc for synthetic constants like zero-extension
  /// padding), and registers the node in the graph. Caller is responsible for
  /// adding edges out of it.
  auto createConstantForSegment(BitSliceSource const &src, Segment const &seg,
                                TextLocation fallbackLoc) -> NetlistNode &;

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

  /// Legacy whole-port LSP walk for a port connection. Used both when
  /// `--resolve-assign-bits` is off and as a fallback when the bit-aligned
  /// path can't build compatible-width slicelists for the two sides.
  void handlePortConnectionLegacy(ast::PortSymbol const &port,
                                  ast::Expression const &expr, bool isOutput,
                                  ast::EvalContext &evalCtx);

  /// Build a slicelist for the formal side of a port connection. Each
  /// netlist Port node belonging to @p symbol becomes a PortNode source
  /// covering the bits it drives.
  auto buildPortSliceList(ast::PortSymbol const &symbol) -> BitSliceList;

  /// Create port nodes for @p symbol, splitting per registered cuts
  /// when `propCutsAcrossPorts` is enabled.
  void materializePortNodes(ast::PortSymbol const &symbol);

  /// Record cut hints from @p instance's port connections onto the
  /// formal ports' internal symbols.
  void recordCutsFromPortConnections(ast::InstanceSymbol const &instance);

  /// If @p symbol lives inside a non-canonical instance body, return the
  /// equivalent value symbol in the canonical body — that's where slang's
  /// AnalysisManager actually stored the drivers. Otherwise returns
  /// @p symbol unchanged. Result is memoized.
  auto canonicalValueSymbol(ast::ValueSymbol const &symbol)
      -> ast::ValueSymbol const &;

  /// Drive one aligned segment of a port connection. Each segment spans
  /// exactly one port node (by construction of the formal slicelist),
  /// so the segment's bits feed into or out of that single port node.
  void drivePortSegment(Segment const &seg, bool isOutput,
                        ast::EvalContext &evalCtx);

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
