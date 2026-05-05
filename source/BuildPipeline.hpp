#pragma once

#include <memory>
#include <vector>

#include <BS_thread_pool.hpp>

#include "PendingRvalueQueue.hpp"

#include "netlist/BuildProfile.hpp"

#include "slang/ast/Symbol.h"

namespace slang::netlist {

class NetlistBuilder;

/// Orchestrates the four-phase netlist build:
///   1. Sequential AST traversal: ports, variables, instance structure;
///      procedural and continuous-assign blocks are collected for later.
///   2. Parallel (or sequential) dispatch of the deferred DFA blocks.
///   3. Drain per-task pending-rvalue buffers into the shared queue.
///   4. Resolve pending rvalues into edges, then tear down the pool.
///
/// Owns phase-scoped state — the thread pool, the deferred-block list,
/// the collecting-phase flag, and the BuildProfile — so the builder
/// itself stays focused on graph mutation and AST visitation.
class BuildPipeline {
public:
  explicit BuildPipeline(NetlistBuilder &builder) : builder(builder) {}

  /// Run phases 1-3.
  void run(ast::Symbol const &root);

  /// Run phase 4 and tear down the thread pool.
  void finalize();

  auto getProfile() const -> BuildProfile const & { return profile; }

  /// Used by NetlistBuilder::handle(ProceduralBlockSymbol) /
  /// handle(ContinuousAssignSymbol) to assert the call happened during
  /// the collecting phase, before deferring.
  auto isCollecting() const -> bool { return collectingPhase; }

  /// Append a deferred block to the work list. Called from the
  /// builder's collecting-phase visitors.
  void deferBlock(ast::Symbol const &symbol, bool isProcedural);

  /// Thread pool shared with PendingRvalueQueue::resolve in Phase 4.
  /// Returns nullptr in sequential builds.
  auto getThreadPool() -> BS::thread_pool<> * { return threadPool.get(); }

private:
  struct DeferredBlock {
    ast::Symbol const *symbol;
    bool isProcedural; // true = ProceduralBlock, false = ContinuousAssign
  };

  void runPhase1(ast::Symbol const &root);
  void runPhase2();
  void runPhase2Sequential();
  void runPhase2Parallel();
  void recordTaskStats(std::vector<DeferredGraphWork> const &allWork);

  NetlistBuilder &builder;
  std::vector<DeferredBlock> deferredBlocks;
  std::unique_ptr<BS::thread_pool<>> threadPool;
  BuildProfile profile;
  bool collectingPhase = false;
};

} // namespace slang::netlist
