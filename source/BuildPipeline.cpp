#include "BuildPipeline.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <mutex>
#include <numeric>

#include "NetlistBuilder.hpp"

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"

namespace slang::netlist {

void BuildPipeline::deferBlock(ast::Symbol const &symbol, bool isProcedural) {
  deferredBlocks.push_back({&symbol, isProcedural});
}

void BuildPipeline::runPhase1(ast::Symbol const &root) {
  using Clock = std::chrono::steady_clock;

  // Clear the main-thread symbol-ref cache so entries from a prior build()
  // (whose Compilation may have been destroyed and whose Symbol addresses
  // may now be reused) cannot produce stale hits.
  builder.clearThreadLocalSymbolRefCache();

  auto t0 = Clock::now();
  collectingPhase = true;
  root.visit(builder);
  collectingPhase = false;
  auto t1 = Clock::now();

  profile.phase1_collectSeconds =
      std::chrono::duration<double>(t1 - t0).count();
  profile.deferredBlockCount = deferredBlocks.size();
  profile.numThreads = builder.options.numThreads;
}

void BuildPipeline::runPhase2Sequential() {
  using Clock = std::chrono::steady_clock;
  auto t = Clock::now();
  builder.clearThreadLocalSymbolRefCache();
  for (auto &block : deferredBlocks) {
    if (block.isProcedural) {
      builder.handleProceduralBlock(
          block.symbol->as<ast::ProceduralBlockSymbol>());
    } else {
      builder.handleContinuousAssign(
          block.symbol->as<ast::ContinuousAssignSymbol>());
    }
  }
  profile.phase2_parallelSeconds =
      std::chrono::duration<double>(Clock::now() - t).count();
}

void BuildPipeline::runPhase2Parallel() {
  using Clock = std::chrono::steady_clock;

  threadPool = std::make_unique<BS::thread_pool<>>(builder.options.numThreads);
  std::mutex exceptionMutex;
  std::exception_ptr pendingException;
  std::vector<DeferredGraphWork> allWork(deferredBlocks.size());

  auto t2 = Clock::now();
  for (size_t i = 0; i < deferredBlocks.size(); ++i) {
    threadPool->detach_task([this, &block = deferredBlocks[i],
                             &work = allWork[i], &exceptionMutex,
                             &pendingException] {
      auto taskStart = Clock::now();
      builder.pendingQueue.setTaskBuffer(&work);
      builder.clearThreadLocalSymbolRefCache();
      SLANG_TRY {
        if (block.isProcedural) {
          builder.handleProceduralBlock(
              block.symbol->as<ast::ProceduralBlockSymbol>());
        } else {
          builder.handleContinuousAssign(
              block.symbol->as<ast::ContinuousAssignSymbol>());
        }
      }
      SLANG_CATCH(const std::exception &) {
        std::lock_guard<std::mutex> lock(exceptionMutex);
        if (!pendingException) {
          pendingException = std::current_exception();
        }
      }
      builder.pendingQueue.setTaskBuffer(nullptr);
      work.elapsedSeconds =
          std::chrono::duration<double>(Clock::now() - taskStart).count();
    });
  }

  threadPool->wait();
  auto t3 = Clock::now();
  profile.phase2_parallelSeconds =
      std::chrono::duration<double>(t3 - t2).count();

  if (pendingException) {
    std::rethrow_exception(pendingException);
  }

  recordTaskStats(allWork);

  auto t4 = Clock::now();
  builder.pendingQueue.drain(allWork, profile);
  profile.phase3_drainSeconds =
      std::chrono::duration<double>(Clock::now() - t4).count();
}

void BuildPipeline::recordTaskStats(
    std::vector<DeferredGraphWork> const &allWork) {
  if (allWork.empty()) {
    return;
  }
  std::vector<double> taskTimes;
  taskTimes.reserve(allWork.size());
  for (auto const &work : allWork) {
    taskTimes.push_back(work.elapsedSeconds);
  }
  std::sort(taskTimes.begin(), taskTimes.end());
  profile.taskMinSeconds = taskTimes.front();
  profile.taskMaxSeconds = taskTimes.back();
  profile.taskTotalSeconds =
      std::accumulate(taskTimes.begin(), taskTimes.end(), 0.0);
  profile.taskMeanSeconds =
      profile.taskTotalSeconds / static_cast<double>(taskTimes.size());
  auto mid = taskTimes.size() / 2;
  profile.taskMedianSeconds = (taskTimes.size() % 2 == 0)
                                  ? (taskTimes[mid - 1] + taskTimes[mid]) / 2.0
                                  : taskTimes[mid];
}

void BuildPipeline::runPhase2() {
  if (builder.options.parallel) {
    runPhase2Parallel();
  } else {
    runPhase2Sequential();
  }
}

void BuildPipeline::run(ast::Symbol const &root) {
  runPhase1(root);
  runPhase2();
  deferredBlocks.clear();
}

void BuildPipeline::finalize() {
  using Clock = std::chrono::steady_clock;
  auto t0 = Clock::now();
  builder.pendingQueue.resolve(threadPool.get());
  threadPool.reset();
  profile.phase4_rvalueSeconds =
      std::chrono::duration<double>(Clock::now() - t0).count();
}

} // namespace slang::netlist
