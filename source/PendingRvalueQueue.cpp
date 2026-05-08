#include "PendingRvalueQueue.hpp"

#include <exception>
#include <mutex>
#include <unordered_map>

#include "NetlistBuilder.hpp"

#include "netlist/Debug.hpp"

namespace slang::netlist {

namespace {

/// Thread-local pointer to the deferred work buffer for the current
/// parallel task. nullptr when running sequentially.
thread_local DeferredGraphWork *threadLocalDeferredWork = nullptr;

} // namespace

void PendingRvalueQueue::enqueue(ast::ValueSymbol const &symbol,
                                 ast::Expression const &lsp,
                                 DriverBitRange bounds, NetlistNode *node,
                                 ast::EdgeKind edgeKind) {
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->pendingRValues.emplace_back(&symbol, &lsp, bounds,
                                                         node, edgeKind);
  } else {
    queue.emplace_back(&symbol, &lsp, bounds, node, edgeKind);
  }
}

void PendingRvalueQueue::setTaskBuffer(DeferredGraphWork *buffer) {
  threadLocalDeferredWork = buffer;
}

void PendingRvalueQueue::drain(std::vector<DeferredGraphWork> &allWork,
                               BuildProfile &profile) {
  // Reserve in one shot so the per-task move-in below doesn't trigger
  // a vector reallocation that would temporarily hold both the old and
  // new backing storage.
  size_t totalPending = queue.size();
  for (auto const &work : allWork) {
    totalPending += work.pendingRValues.size();
  }
  queue.reserve(totalPending);

  for (auto &work : allWork) {
    profile.deferredPendingRValueCount += work.pendingRValues.size();
    queue.insert(queue.end(),
                 std::make_move_iterator(work.pendingRValues.begin()),
                 std::make_move_iterator(work.pendingRValues.end()));
    // Release this task's buffer immediately. Otherwise its storage
    // stays alive until allWork goes out of scope at the end of
    // runPhase2Parallel, roughly doubling peak memory for the queue.
    std::vector<PendingRvalue>().swap(work.pendingRValues);
  }
  profile.drain_pendingRValuesSeconds = 0;
  profile.drain_mergesSeconds = 0;
}

void PendingRvalueQueue::emitEdgesFor(PendingRvalue const &pending) {
  if (pending.node == nullptr) {
    return;
  }
  DEBUG_PRINT("Processing pending R-value {}{}\n", pending.symbol->name,
              toString(pending.bounds));

  auto symRef = builder.toSymbolRef(*pending.symbol);

  // If there is state variable matching this rvalue.
  if (auto *stateNode = builder.getVariable(*pending.symbol, pending.bounds)) {
    builder.addDependency(*stateNode, *pending.node, symRef, pending.bounds,
                          pending.edgeKind);
    return;
  }

  // Otherwise, walk the driver intervals that overlap the pending
  // range, emitting an edge per driver annotated with the portion of
  // the driver's range that the pending R-value actually reads. When
  // the interval map has split a single contiguous driver range into
  // abutting sub-intervals, multiple emissions collide on the same
  // (source, target) edge and NetlistEdge::setVariable unions their
  // bounds back into the original range.
  builder.driverMap.forEachDriverInterval(
      builder.drivers, *pending.symbol, pending.bounds,
      [&](DriverBitRange intervalBounds, DriverList const &driverList) {
        auto edgeBounds = intervalBounds.intersection(pending.bounds);
        if (!edgeBounds.has_value()) {
          return;
        }
        for (auto const &source : driverList) {
          if (source.node != nullptr) {
            builder.addDependency(*source.node, *pending.node, symRef,
                                  *edgeBounds, pending.edgeKind);
          }
        }
      });
}

void PendingRvalueQueue::resolveSequential() {
  for (auto &pending : queue) {
    emitEdgesFor(pending);
  }
  queue.clear();
}

void PendingRvalueQueue::resolveParallel(BS::thread_pool<> &threadPool) {
  // Group pending R-values by target node so each target's incoming
  // edges are emitted from a single thread. Sorting the queue in place
  // + a one-shot run-start index avoids a per-target
  // std::vector<size_t> in a hash map, which on large designs can be
  // many MB of transient overhead. The original queue order is not
  // preserved, but the queue is cleared at the end of this function so
  // that has no observable effect.
  std::ranges::sort(queue, std::less<NetlistNode *>{}, &PendingRvalue::node);

  // Entries with node == nullptr cluster at the front; skip them.
  auto firstReal = std::ranges::find_if(
      queue, [](PendingRvalue const &p) { return p.node != nullptr; });
  size_t firstRealIdx = static_cast<size_t>(firstReal - queue.begin());

  // Build run-start indices for each distinct target.
  // Run r covers queue[runStarts[r] .. runStarts[r + 1]).
  std::vector<size_t> runStarts;
  if (firstRealIdx < queue.size()) {
    runStarts.push_back(firstRealIdx);
    for (size_t i = firstRealIdx + 1; i < queue.size(); ++i) {
      if (queue[i].node != queue[i - 1].node) {
        runStarts.push_back(i);
      }
    }
    runStarts.push_back(queue.size());
  }

  size_t numRuns = runStarts.empty() ? 0 : runStarts.size() - 1;

  std::mutex exceptionMutex;
  std::exception_ptr pendingException;

  threadPool.detach_blocks(
      static_cast<size_t>(0), numRuns, [&](size_t begin, size_t end) {
        builder.clearThreadLocalSymbolRefCache();
        for (size_t r = begin; r < end; ++r) {
          for (size_t i = runStarts[r]; i < runStarts[r + 1]; ++i) {
            SLANG_TRY { emitEdgesFor(queue[i]); }
            SLANG_CATCH(const std::exception &) {
              std::lock_guard<std::mutex> lock(exceptionMutex);
              if (!pendingException) {
                pendingException = std::current_exception();
              }
            }
          }
        }
      });

  threadPool.wait();

  if (pendingException) {
    std::rethrow_exception(pendingException);
  }

  queue.clear();
}

void PendingRvalueQueue::resolve(BS::thread_pool<> *threadPool) {
  if (!builder.options.parallel || threadPool == nullptr ||
      queue.size() < builder.options.parallelRValueThreshold) {
    resolveSequential();
    return;
  }
  resolveParallel(*threadPool);
}

} // namespace slang::netlist
