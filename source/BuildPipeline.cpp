#include "BuildPipeline.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <mutex>
#include <numeric>

#include "NetlistBuilder.hpp"

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"

namespace slang::netlist {

namespace {

/// CPU time consumed by the calling thread so far, used to separate the
/// cost of a task from the wall time it spent sharing a core.
auto threadCpuSeconds() -> double {
  timespec ts{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
    return 0.0;
  }
  return static_cast<double>(ts.tv_sec) +
         static_cast<double>(ts.tv_nsec) * 1e-9;
}

} // namespace

void BuildPipeline::deferBlock(ast::Symbol const &symbol, bool isProcedural) {
  deferredBlocks.push_back({&symbol,
                            isProcedural
                                ? DeferredBlock::Kind::Procedural
                                : DeferredBlock::Kind::ContinuousAssign});
}

void BuildPipeline::deferNetInitializer(ast::NetSymbol const &symbol,
                                        ast::Expression const &assignment) {
  deferredBlocks.push_back(
      {&symbol, DeferredBlock::Kind::NetInitializer, &assignment});
}

void BuildPipeline::runBlock(DeferredBlock const &block) {
  switch (block.kind) {
  case DeferredBlock::Kind::Procedural:
    builder.handleProceduralBlock(
        block.symbol->as<ast::ProceduralBlockSymbol>());
    break;
  case DeferredBlock::Kind::ContinuousAssign:
    builder.handleContinuousAssign(
        block.symbol->as<ast::ContinuousAssignSymbol>());
    break;
  case DeferredBlock::Kind::NetInitializer:
    builder.handleNetInitializer(block.symbol->as<ast::NetSymbol>(),
                                 *block.assignment);
    break;
  }
}

void BuildPipeline::runPhase1(ast::Symbol const &root) {
  using Clock = std::chrono::steady_clock;

  // Entries cached by a prior build() key on Symbol addresses that a
  // since-destroyed Compilation may have released, so tag this build with
  // a fresh generation and let each thread drop its stale entries lazily.
  builder.beginBuildGeneration();

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
  auto cpuStart = threadCpuSeconds();
  for (auto &block : deferredBlocks) {
    runBlock(block);
  }
  profile.taskCpuTotalSeconds = threadCpuSeconds() - cpuStart;
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
      auto taskCpuStart = threadCpuSeconds();
      builder.pendingQueue.setTaskBuffer(&work);
      SLANG_TRY { runBlock(block); }
      SLANG_CATCH(const std::exception &) {
        std::lock_guard<std::mutex> lock(exceptionMutex);
        if (!pendingException) {
          pendingException = std::current_exception();
        }
      }
      builder.pendingQueue.setTaskBuffer(nullptr);
      work.cpuSeconds = threadCpuSeconds() - taskCpuStart;
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
  double cpuTotal = 0;
  for (auto const &work : allWork) {
    taskTimes.push_back(work.elapsedSeconds);
    cpuTotal += work.cpuSeconds;
  }
  profile.taskCpuTotalSeconds = cpuTotal;
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
  auto t1 = Clock::now();
  profile.phase4_rvalueSeconds = std::chrono::duration<double>(t1 - t0).count();

  builder.graph.mergeParallelEdges();
  profile.phase5_mergeEdgesSeconds =
      std::chrono::duration<double>(Clock::now() - t1).count();
}

} // namespace slang::netlist
