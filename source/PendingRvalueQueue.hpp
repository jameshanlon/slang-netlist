#pragma once

#include <vector>

#include <BS_thread_pool.hpp>

#include "PendingRValue.hpp"

#include "netlist/BuildProfile.hpp"

namespace slang::netlist {

class NetlistBuilder;

/// Thread-local accumulator for deferred pending R-values produced by
/// one parallel Phase 2 task. Held by value in a per-task slot so the
/// dispatch loop can also record wall-clock time.
struct DeferredGraphWork {
  std::vector<PendingRvalue> pendingRValues;
  double elapsedSeconds = 0; // Wall-clock time for this task.
};

/// Owns the pending-rvalue queue for the build. R-values are deferred
/// until after all drivers have been registered, then resolved into
/// edges in Phase 4.
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

  /// Set or clear the current thread's per-task buffer. Pass nullptr
  /// to revert to the shared-queue path.
  void setTaskBuffer(DeferredGraphWork *buffer);

  /// Move the contents of @p allWork's per-task buffers into the
  /// main queue. Updates `profile.deferredPendingRValueCount`.
  void drain(std::vector<DeferredGraphWork> &allWork, BuildProfile &profile);

  /// Resolve every queued pending R-value into edges. Picks
  /// sequential or parallel based on builder options and the size
  /// of the queue. @p threadPool may be null for sequential builds.
  void resolve(BS::thread_pool<> *threadPool);

private:
  /// Sequential path: walk the queue and emit edges directly.
  void resolveSequential();

  /// Parallel path: partition by target node and dispatch chunks.
  void resolveParallel(BS::thread_pool<> &threadPool);

  /// Emit the edges implied by one pending R-value.
  void emitEdgesFor(PendingRvalue const &pending);

  NetlistBuilder &builder;
  std::vector<PendingRvalue> queue;
};

} // namespace slang::netlist
