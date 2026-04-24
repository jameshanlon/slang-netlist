# Change log

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unreleased]

Library features:
* Preserve bit alignment across concatenations, replications, same-width and
  widening conversions, and equal-width conditional operators on both the
  assignment and port-connection paths. Enabled by default; set
  `BuilderOptions::resolveAssignBits = false` to restore the legacy
  whole-expression behaviour where each LSP on one side of an assignment fans
  into every LSP on the other side. Expression shapes the decomposition cannot
  handle (width-mismatched assignments or port connections, narrowing
  conversions, non-integral types, etc.) transparently fall back to the legacy
  walk.

Driver features:
* Add `--no-resolve-assign-bits` to restore the legacy whole-expression driver
  fan-out behaviour.

Python bindings:
* Add `resolve_assign_bits` kwarg to `NetlistGraph.build`.

## [v0.5.1] 2026-04-19

Library features:
* Attempt to improve parallel performance with threads:
  - Parallelise Phase 4 R-value resolution across a thread pool.
  - Restructure parallel Phase 2 DFA to use slang's `AnalysisManager` directly.
  - Make `DirectedGraph`, `ValueTracker`, and `VariableTracker` thread-safe with
    fine-grained per-node and per-key locking.
  - Add per-slot allocators in `ValueTracker` and `VariableTracker` to eliminate
    allocator contention.
  - Eliminate `FileTable` contention in the phase-two builder path.
  - Cache `SymbolReference` construction and hoist per-rvalue `BumpAllocator` to
    `DataFlowAnalysis` member to reduce allocations in hot paths.
* Update slang dependency and refactor `LSPUtilities`.

Driver features:
* Add build profiling instrumentation and `--stats` reporting with per-phase
  timing breakdowns.

Bug fixes:
* Fix thread-unsafe lazy AST resolution in parallel DFA pass by forcing
  statement visiting in the sequential `VisitAll` phase.
* Fix null `DriverInfo::node` in `addDriversToNode`.
* Fix handling of always blocks with non-timed bodies.
* Fix assertion in `setVariable` for non-contiguous ranges.

## [v0.5.0] 2026-04-12

Library features:
* Add combinational fan-in and fan-out queries on `NetlistGraph`.
* Add wildcard and regex node search via `findNodes()` and `findNodesRegex()`.
* Add support for finding combinational paths between netlist nodes.
* Add lookup of netlist nodes by name and bit range.
* Resolve `getDrivers()` via `NetlistGraph` rather than builder-internal state.
* Make `NetlistBuilder` and supporting classes private implementation details behind a `NetlistGraph::build()` method.
* Skip uninstantiated instances in `VisitAll` to avoid spurious elaboration.

Driver features:
* Add `--fan-out` and `--fan-in` commands to report combinational fan cones.
* Add `--find` and `--find-regex` commands to search for named nodes.
* Report execution time and peak memory statistics.

Python bindings:
* Expose `build()`, `get_drivers()`, `get_comb_fan_out()`, `get_comb_fan_in()`,
  `find_nodes()`, and `find_nodes_regex()` on `NetlistGraph`.

Bug fixes:
* Fix non-blocking assignment assertion.
* Fix Python netlist builder setup.
* Fix GCC build.

## [v0.4.0] 2026-03-15

Library features:
* Remove slang AST references from the netlist graph, replacing `SourceLocation`
  with a self-contained `TextLocation` type. This decouples the graph from the
  compilation object and enables serialisation.
* Add JSON serialisation and deserialisation of `NetlistGraph` via
  `NetlistSerializer`. The CLI exposes `--save-netlist` and `--load-netlist`
  flags.
* Eliminate all mutexes from the parallel netlist build by restructuring shared
  state.

Testing:
* Expand unit test suite, adding dedicated test files for diagnostics, driver
  map, DOT rendering, report visitors, serialisation round-trips, path finding, and value tracker edge cases.

## [v0.3.0] 2026-03-02

Library features:
* Parallelise `NetlistBuilder` using a two-phase approach: phase 1 sequentially
  visits the AST to build ports, variables and instance structure; phase 2
  dispatches procedural-block and continuous-assignment analysis to a thread
  pool. Thread safety is provided by a concurrent map in `ValueTracker` and
  internal mutexes in `NetlistGraph` and `VariableTracker`.

Bug fixes:
* Fix crashes in `DriverMap` and `ValueTracker` when handling split driver
  intervals.
* Fix crash due to null current-state node in `DataFlowAnalysis`.
* Fix `IntervalMap::difference` when the subtracted range starts at the current
  iterator position.
* Fix `IntervalMap` assertion triggered by `DriverBitRange` with descending
  bounds (e.g. `[3:0]` stored as `ConstantRange{3, 0}`).
* Propagate return code correctly after a parsing or elaboration error.
* Skip automatic variables in data-flow analysis.
* Remove spurious `UNREACHABLE` for non-interface variables and modport ports.

## [v0.2.0] 2025-12-28

Library features:
* Improve the performance of merging drivers by avoiding cloning interval maps and adding a special case for matching ranges.
* Improve labeling of the bit ranges on netlist edges.
* Add basic combinational loop detection.

Driver features:
* Add options to report variables, ports and registers to stdout, formatted as tabular data.

Python bindings:
* Add binding to iterate over nodes in the netlist.

Bug fixes:
* Handling of driver overlap cases.

## [v0.1.0] 2025-11-22

This is the initial release of slang netlist. It represents an early milestone
in the project since it was separated from upstream slang and mostly rewritten
on the way, but includes a baseline set of features, infrastructure and
documentation.
