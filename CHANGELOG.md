
# Change log

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unreleased]

Library features:
* Record the resolved hierarchical path of each black-boxed instance on the
  graph (`NetlistGraph::getBlackBoxPaths`) and add
  `NetlistGraph::getBlackBoxCoverage(NetlistNode const&)`, which classifies
  any node as outside, on the boundary of, or contained in a black box.
* Add `NetlistGraph::getBitDrivers(name, bounds)`, returning the per-bit
  drivers of a symbol (one entry per contributing edge, clipped to the
  queried range), unlike `getDrivers` which returns the deduplicated node
  set.

Library changes:
* Bump the netlist JSON format to version 3: black-box instance paths are
  serialised in a new `blackBoxes` field.

Driver features:
* Add `--sensitivity <name>`, reporting the clocks and resets gating a named
  node (a register's own clocking edges, or the union over registers in a
  combinational node's fan-out).
* Add `--constant-drivers <name>`, reporting the constant values driving a
  named node when it is tied off to literals.
* Add `--format <table|json>` and `-o`/`--output <file>` to the tabular query
  commands (`--report-registers`, `--find`, `--find-regex`, `--fan-out`,
  `--fan-in`, `--sensitivity`, `--constant-drivers`); `json` emits an array of
  objects keyed by the column headers.
* Add `--drivers <name[bit-range]>`, reporting per bit which node drives a
  named signal. An optional bit-range suffix (`m.sig[3:0]`, `m.sig[2]`)
  narrows the query; without one the whole signal is reported.
* `--netlist-dot` can now be scoped: combined with `--fan-out`, `--fan-in`, or
  `--from`/`--to` it renders only that cone or path (the induced subgraph)
  instead of the whole netlist.
* `--from` and `--to` may now be used on their own: a lone `--from` reports the
  combinational fan-out cone (like `--fan-out`) and a lone `--to` the fan-in
  cone (like `--fan-in`), for both tabular output and scoped `--netlist-dot`.
* Add `--scope <path>` and `--name <pattern>` filters to the node-listing query
  commands (`--report-registers`, `--find`, `--find-regex`, `--fan-out`,
  `--fan-in`, `--sensitivity`). `--scope` restricts output to a hierarchical
  subtree (segment-aware, so `top.cpu` excludes `top.cpu2`); `--name` restricts
  to nodes whose path matches a glob. Both are repeatable and combine as
  in-scope AND name-match.

## [v0.10.0]

Library features:
* Add `NetlistGraph::getConstantDrivers(NetlistNode&)`, which returns the
  `Constant` nodes feeding a node when its combinational fan-in bottoms out
  only at constants, and an empty result otherwise (e.g. when a `State` node
  or an undriven top-level input reaches it).
* Extend the wildcard syntax used by symbol-selection and report filters:
  `*` matches a single `.`-separated segment, `**`/`...` match recursively
  across segments, and `?` matches a single non-`.` character.

Library changes:
* Black-box matching now uses the wildcard syntax against module definition
  names and hierarchical instance paths, rather than plain name/path matching.

Driver features:
* Add `slang-report` CLI, a companion to `slang-netlist` that surfaces
  AST-level information during design exploration without re-compiling.
  The equivalent `--report-*` flags (and `--ast-json`) are removed from
  `slang-netlist`.
  - The `--ports`, `--variables`, and `--drivers` modes accept
    `--format=<table|json>` (default `table`); JSON is emitted via slang's
    `JsonWriter`.
  - Enrich the tabular reports: `--ports` gains width and net-type columns;
    `--variables` gains type, width, kind, and driver-count columns and now
    lists nets alongside variables.
  - `--scope <path>` restricts all four modes to one or more named
    hierarchical scopes.
  - `--name <pattern>` filters the tabular modes by hierarchical-path glob;
    multiple patterns combine with OR semantics.
  - `-o,--output <file>` writes output to a file instead of stdout (`-`
    for stdout).

Python bindings:
* Add `NetlistGraph.get_constant_drivers()`.
* Remove `ReportDrivers` and `ReportVariables`; use `slang-report` instead.

## [v0.9.0] 2026-05-08

Library features:
* Add a black-box mechanism (`BuilderOptions::blackBoxes`,
  `--black-box`, `black_boxes=` in Python) that skips body traversal
  for matched instances, keeping only port-boundary connectivity.
  Names match either a module definition or a hierarchical instance
  path.

Library changes:
* Reduce peak memory and improve build throughput. Cumulative effect on
  RTLMeter designs: roughly 19% lower peak RSS and 5% lower netlist-build
  wall time (Phase 4 alone runs ~30% faster):
  - `SymbolReference` is interned per graph in a new `SymbolTable` and
    edges hold a pointer into it instead of copies of the name/path/location.
  - `DirectedGraph` edges are owned by their source node via `unique_ptr`,
    with the target storing a raw pointer (replacing `shared_ptr` on both
    ends and dropping the per-edge control block + atomic refcount).
  - Per-node `outEdgeIndex` is allocated lazily, only once a node's
    out-degree exceeds 16.
  - `PendingRvalueQueue::resolveParallel` partitions by target via an
    in-place sort + run-start vector instead of an
    `unordered_map<NetlistNode*, vector<size_t>>`.
  - `drain` now releases per-task buffers as it consumes them.

## [v0.8.0] 2026-05-06

Library features:
* Add `NetlistGraph::getSensitivity(NetlistNode)` to query the clock/edge
  sensitivity of a node from the graph itself, rather than reaching into
  builder-internal pending-r-value state.

Python bindings:
* Expose `VisitAll` for forcing lazy AST construction before freezing.
* Expose `unfreeze_compilation()` wrapping slang's `Compilation::unfreeze()`,
  which pyslang does not bind.

Bug fixes:
* Fix a ternary nested inside an outer concatenation losing edges from
  its arms when `baseOffset > 0`.
* Fix non-canonical sibling instance pairing so driver lookups resolve
  through to the truly canonical body instead of a stale alias.

Library changes:
* Internal refactor of `NetlistBuilder`: extracted `BuildPipeline`,
  `CanonicalBodyResolver`, `NodeFactory`, `PendingRvalueQueue` and
  `PortConnectionHandler` into their own translation units. No
  observable behaviour change.

## [v0.7.0] 2026-04-29

Library features:
* Represent constant-value drivers as a new `Constant` netlist node kind.
  These are inferred for pure-literal RHSs, constant-foldable expressions
  (including narrowing conversions of literals like `b = 1`), zero-extension bits
  of widening conversions, literal port connections, and constant arms of
  conditional operators.  Sign-extension padding still produces no driver. The
  `NetlistDot`, `NetlistSerializer` (best-effort integer round-trip via
  `SVInt::fromString`) and `NodeKind::Constant` filter paths all handle the new
  kind.
* Propagate concat-induced cut points across module port boundaries so that
  paths through concatenated ports stay bit-precise. The formal port nodes
  and the module-internal assignments are split at the same bit boundaries
  as the actual concat, removing cross-bit edges through patterns like
  `sub u(.x({b,a}), .y({d,c}))`. Cut hints flow down the hierarchy via
  pass-through ports. Enabled by default; set
  `BuilderOptions::propCutsAcrossPorts = false` to restore the legacy
  whole-word port behaviour.

Driver features:
* Add `--no-prop-cuts-across-ports` to disable cross-port cut propagation.

Bug fixes:
* Fix a bug where every non-canonical instance of a
  multi-instantiated module was left without per-bit connectivity. Slang's
  `AnalysisManager` deduplicates equivalent instance bodies and stores drivers
  only against the canonical one, so previously only one instance of each module
  received port-node and internal-assignment wiring; every other instance appeared
  as dangling nodes and any path through it was silently dropped. `NetlistBuilder`
  now redirects driver lookups for non-canonical bodies to their canonical
  counterpart so each instance gets its own independent subgraph.

Library changes:
* Move parallel-execution settings (`parallel`, `numThreads`,
  `parallelRValueThreshold`) into `BuilderOptions`. `NetlistGraph::build`
  now takes a single `BuilderOptions` argument instead of separate parameters.

## [v0.6.0] 2026-04-24

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
