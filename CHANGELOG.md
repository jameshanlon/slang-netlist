# Change log

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unreleased]

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
