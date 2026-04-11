# Change log

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unreleased]

Library features:
* Add support for finding combinational paths between netlist nodes.
* Add lookup of netlist nodes by name and bit range.
* Resolve `getDrivers()` via `NetlistGraph` rather than builder-internal state.
* Skip uninstantiated instances in `VisitAll` to avoid spurious elaboration.

Driver features:
* Report execution time and peak memory statistics.

Bug fixes:
* Fix non-blocking assignment assertion.
* Fix Python netlist builder setup.
* Fix GCC build.

Testing:
* Add `VariableTracker` unit tests, additional SV construct and loop coverage.
* Enable more RTL Meter tests and move design configuration to a YAML file.

Infrastructure:
* Add a benchmark workflow and enable ccache in CI workflows.

## [v0.4.0]

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

## [v0.3.0]

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
