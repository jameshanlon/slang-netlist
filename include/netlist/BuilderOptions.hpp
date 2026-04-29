#pragma once

#include <cstddef>

namespace slang::netlist {

/// Caller-supplied options that tune how the netlist graph is built.
struct BuilderOptions {
  /// When true (default), decompose concatenations, replications,
  /// conversions, and equal-width conditional operators so that
  /// bit-level dependencies between assignment LHS and RHS (and across
  /// port connections) are preserved. When false, each LSP on one side
  /// of an assignment fans into every LSP on the other side, matching
  /// the behaviour of releases before bit-aligned resolution landed.
  bool resolveAssignBits = true;

  /// When true (default), propagate concat-induced cut points across
  /// module port boundaries so that paths through concatenated ports
  /// stay bit-precise. When false, port nodes and module-internal
  /// assignments are whole-word at port boundaries.
  bool propCutsAcrossPorts = true;

  /// When true (default), dispatch deferred DFA work items in parallel
  /// across a thread pool during Phase 2 of the build, and use the
  /// parallel R-value resolution path in Phase 4 when the pending
  /// R-value count exceeds `parallelRValueThreshold`.
  bool parallel = true;

  /// Size of the thread pool used when `parallel` is true. 0 (default)
  /// means use hardware concurrency.
  unsigned numThreads = 0;

  /// Minimum number of pending R-values required before Phase 4 uses
  /// the parallel resolution path.
  std::size_t parallelRValueThreshold = 1000;
};

} // namespace slang::netlist
