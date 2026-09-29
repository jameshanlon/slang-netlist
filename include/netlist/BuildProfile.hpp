#pragma once

#include <cstddef>

namespace slang::netlist {

/// Profiling data collected during netlist graph construction.
struct BuildProfile {
  // Phase-level timings (seconds).
  double phase1_collectSeconds = 0;    // Sequential AST traversal
  double phase2_parallelSeconds = 0;   // Parallel DFA dispatch + wait
  double phase3_drainSeconds = 0;      // Sequential drain of deferred work
  double phase4_rvalueSeconds = 0;     // Sequential pending R-value resolution
  double phase5_mergeEdgesSeconds = 0; // Sequential merge of parallel edges

  // Drain sub-phase timings (seconds).
  double drain_pendingRValuesSeconds = 0;
  double drain_mergesSeconds = 0;

  // Work item counts.
  size_t deferredBlockCount = 0;
  size_t deferredPendingRValueCount = 0;

  // Per-task timing statistics (seconds).
  double taskMinSeconds = 0;
  double taskMaxSeconds = 0;
  double taskMeanSeconds = 0;
  double taskMedianSeconds = 0;
  // Summed over concurrently running tasks, so under a saturated pool
  // this approaches numThreads * phase2_parallelSeconds regardless of
  // how much work was done. Use taskCpuTotalSeconds to compare the cost
  // of a build across thread counts.
  double taskTotalSeconds = 0; // Sum of all task wall times

  // Sum of the CPU time consumed by each task. Unlike the wall-time sum
  // this is independent of how many tasks ran at once, so comparing it
  // across thread counts shows what parallelism actually costs.
  double taskCpuTotalSeconds = 0;

  unsigned numThreads = 0;

  /// Total time across all phases.
  [[nodiscard]] auto totalSeconds() const -> double {
    return phase1_collectSeconds + phase2_parallelSeconds +
           phase3_drainSeconds + phase4_rvalueSeconds +
           phase5_mergeEdgesSeconds;
  }
};

} // namespace slang::netlist
