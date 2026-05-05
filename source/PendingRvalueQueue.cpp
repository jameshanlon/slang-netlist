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
                                 DriverBitRange bounds, NetlistNode *node) {
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->pendingRValues.emplace_back(&symbol, &lsp, bounds,
                                                         node);
  } else {
    queue.emplace_back(&symbol, &lsp, bounds, node);
  }
}

void PendingRvalueQueue::setTaskBuffer(DeferredGraphWork *buffer) {
  threadLocalDeferredWork = buffer;
}

void PendingRvalueQueue::drain(std::vector<DeferredGraphWork> &allWork) {
  for (auto &work : allWork) {
    builder.profile.deferredPendingRValueCount += work.pendingRValues.size();
    for (auto &pr : work.pendingRValues) {
      queue.push_back(std::move(pr));
    }
  }
  builder.profile.drain_pendingRValuesSeconds = 0;
  builder.profile.drain_mergesSeconds = 0;
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
    builder.addDependency(*stateNode, *pending.node, symRef, pending.bounds);
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
                                  *edgeBounds);
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

void PendingRvalueQueue::resolveParallel() {
  // Partition by target node.
  std::unordered_map<NetlistNode *, std::vector<size_t>> partitions;
  for (size_t i = 0; i < queue.size(); ++i) {
    if (queue[i].node != nullptr) {
      partitions[queue[i].node].push_back(i);
    }
  }

  // Flatten partition keys for chunked dispatch.
  std::vector<NetlistNode *> targets;
  targets.reserve(partitions.size());
  for (auto &[node, _] : partitions) {
    targets.push_back(node);
  }

  std::mutex exceptionMutex;
  std::exception_ptr pendingException;

  // Dispatch chunks of target nodes to threads.
  builder.threadPool->detach_blocks(
      static_cast<size_t>(0), targets.size(), [&](size_t begin, size_t end) {
        builder.clearThreadLocalSymbolRefCache();
        for (size_t t = begin; t < end; ++t) {
          auto *targetNode = targets[t];
          for (size_t idx : partitions[targetNode]) {
            SLANG_TRY { emitEdgesFor(queue[idx]); }
            SLANG_CATCH(const std::exception &) {
              std::lock_guard<std::mutex> lock(exceptionMutex);
              if (!pendingException) {
                pendingException = std::current_exception();
              }
            }
          }
        }
      });

  builder.threadPool->wait();

  if (pendingException) {
    std::rethrow_exception(pendingException);
  }

  queue.clear();
}

void PendingRvalueQueue::resolve() {
  if (!builder.options.parallel || !builder.threadPool ||
      queue.size() < builder.options.parallelRValueThreshold) {
    resolveSequential();
    return;
  }
  resolveParallel();
}

} // namespace slang::netlist
