#pragma once

#include <cstddef>

namespace slang::netlist {

/// Profiling data collected during netlist graph construction.
struct BuildProfile {
  // Phase-level timings (seconds).
  double phase1_collectSeconds = 0; // Sequential AST traversal
  double phase2_parallelSeconds = 0; // Parallel DFA dispatch + wait
  double phase3_drainSeconds = 0; // Sequential drain of deferred work
  double phase4_rvalueSeconds = 0; // Sequential pending R-value resolution

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
  double taskTotalSeconds = 0; // Sum of all task wall times

  unsigned numThreads = 0;

  /// Total time across all phases.
  [[nodiscard]] auto totalSeconds() const -> double {
    return phase1_collectSeconds + phase2_parallelSeconds +
           phase3_drainSeconds + phase4_rvalueSeconds;
  }
};

} // namespace slang::netlist
