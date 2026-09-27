#pragma once

#include <vector>

#include <BS_thread_pool.hpp>

#include "PendingRValue.hpp"

#include "netlist/BuildProfile.hpp"

namespace slang::netlist {

class NetlistBuilder;

/// Thread-local accumulator for deferred graph work produced by one
/// parallel Phase 2 task. Held by value in a per-task slot so the
/// dispatch loop can also record wall-clock time.
struct DeferredGraphWork {
  std::vector<PendingRvalue> pendingRValues;
  std::vector<PendingVariableHookup> variableHookups;
  double elapsedSeconds = 0; // Wall-clock time for this task.
  double cpuSeconds = 0;     // CPU time consumed by this task.
};

/// Owns the queues of graph work deferred out of Phase 2. R-values and
/// variable hookups both resolve by looking up nodes that other blocks
/// may still be creating, so both wait until every block has finished
/// and are turned into edges in Phase 4.
///
/// Thread-local routing: during parallel Phase 2 each task's
/// `enqueue` push goes into a per-task `DeferredGraphWork` buffer to
/// avoid contention on the shared queue. After Phase 2 the per-task
/// buffers are drained back into the main queue. Outside of Phase 2
/// (sequential Phase 2, the modport fast path inside the builder),
/// `enqueue` pushes directly to the main queue.
class PendingRvalueQueue {
public:
  explicit PendingRvalueQueue(NetlistBuilder &builder) : builder(builder) {}

  /// Push a pending R-value onto either the current task's
  /// thread-local buffer (if one is set) or the main queue. @p edgeKind
  /// is forwarded to the resolved edge; pass `None` for ordinary r-values
  /// and `PosEdge`/`NegEdge`/`BothEdges` for procedural-block sensitivity
  /// signals.
  void enqueue(ast::ValueSymbol const &symbol, ast::Expression const &lsp,
               DriverBitRange bounds, NetlistNode *node,
               ast::EdgeKind edgeKind = ast::EdgeKind::None);

  /// Defer connecting @p driver to the node representing @p variable over
  /// @p bounds. Routed to the per-task buffer or the shared queue as for
  /// enqueue.
  void enqueueVariableHookup(NetlistNode &driver, ast::Symbol const &variable,
                             DriverBitRange bounds,
                             SymbolReference const *edgeSymbol);

  /// Set or clear the current thread's per-task buffer. Pass nullptr
  /// to revert to the shared-queue path.
  void setTaskBuffer(DeferredGraphWork *buffer);

  /// Move the contents of @p allWork's per-task buffers into the
  /// main queues. Updates `profile.deferredPendingRValueCount`.
  void drain(std::vector<DeferredGraphWork> &allWork, BuildProfile &profile);

  /// Resolve every queued variable hookup and pending R-value into
  /// edges. Picks sequential or parallel based on builder options and the
  /// size of the queue. @p threadPool may be null for sequential builds.
  void resolve(BS::thread_pool<> *threadPool);

private:
  /// Emit the edges for every queued variable hookup.
  void resolveVariableHookups();

  /// Sequential path: walk the queue and emit edges directly.
  void resolveSequential();

  /// Parallel path: partition by target node and dispatch chunks.
  void resolveParallel(BS::thread_pool<> &threadPool);

  /// Emit the edges implied by one pending R-value.
  void emitEdgesFor(PendingRvalue const &pending);

  NetlistBuilder &builder;
  std::vector<PendingRvalue> queue;
  std::vector<PendingVariableHookup> hookupQueue;
};

} // namespace slang::netlist
