---
name: architecture
description: Use when reasoning about slang-netlist's core data model, adding query APIs, changing how drivers/edges are recorded, or debugging bit-range / driver-tracking issues. Triggers on NetlistGraph, NetlistEdge, DriverBitRange, ValueTracker, DriverMap, hookupOutputPort, processPendingRvalues, driver query, bit range, interval map.
---

# Architecture

Design principles, data-model invariants, and gotchas for slang-netlist. Extend this file as new invariants are discovered — it is intentionally load-bearing, not a narrative.

## Design principles

### NetlistGraph is the ground truth for data dependencies

After `NetlistBuilder::finalize()` the graph already captures every real driver relationship as an annotated edge. Driver / connectivity queries belong on `NetlistGraph`, not on builder internals (`ValueTracker`, `DriverMap`, `VariableTracker`). Callers should never need to unfreeze the compilation, resolve AST symbols, or reach into builder state to answer a query.

`ValueTracker` / `DriverMap` are **construction scratch**. They exist to decide which edges to emit during the parallel Phase-2 analysis; they are not part of any public query path. If you find yourself wanting to expose them, the right move is almost always to add a new query on `NetlistGraph` that walks edges instead.

### Reuse existing graph annotations before adding structure

`NetlistEdge` already carries `symbol: SymbolReference`, `bounds: DriverBitRange`, and `edgeKind: ast::EdgeKind`. Before adding a new node subtype, a new edge field, or a parallel map, check whether the query can be answered by scanning edges and filtering on these annotations. New node kinds are expensive: they affect `PathFinder`, `CombLoops`, serialization, dot rendering, and every visitor pattern.

## Core data model

### NetlistGraph / NetlistNode / NetlistEdge

- `NetlistGraph` extends `DirectedGraph<NetlistNode, NetlistEdge>`. **Multi-edges are not permitted**: `Node::addEdge` dedups by target, so two calls to `addEdge(src, tgt)` return the *same* edge. Anything that wants to annotate per-emission bit-range precision must account for this.
- `NetlistNode` kinds: `Port`, `Variable`, `Assignment`, `Conditional`, `Case`, `Merge`, `State`. Concrete kinds carry `bounds` (bit range on the underlying symbol) only for Port/Variable/State.
- `NetlistEdge` fields:
  - `symbol` — hierarchical path + name of the driven symbol **that flows through this edge** (not the source or target's own name).
  - `bounds` — the bit range of `symbol` driven by the edge's source. Semantically "driver drives these bits of this symbol, and the result reaches the target".
  - `edgeKind` — clock sensitivity; used by `CombLoops` to filter non-combinational edges.

### Edge bounds: union on same-symbol collision

Because `addEdge` dedups, the same `(src, tgt)` pair can receive multiple emissions with different bit ranges when the interval map has split a single contiguous driver range into sub-intervals (e.g. `{[0,1]→A, [2,2]→A, [3,3]→A}`). `NetlistEdge::setVariable` handles the collision by unioning the incoming bounds with the stored bounds *iff the hierarchical symbol matches*.

**The union is contiguous-only.** Use `DriverBitRange::unionWith` — it asserts `isContiguousWith`. This is deliberate: a non-contiguous "union" would silently over-claim bits the source doesn't drive (e.g. `t[10:0] = a; t[3] = b;` splits A into `[0,2]` and `[4,10]`, and hulling those would falsely report A as a driver of bit 3). If that assertion ever fires, the graph representation is insufficient for the case — the correct fix is multi-edges or a per-edge bounds list, not relaxing the assertion.

### DriverBitRange helpers

Prefer these over hand-rolled `std::min` / `std::max` arithmetic:

- `isContiguousWith(other)` — true iff the two ranges abut or overlap.
- `unionWith(other)` — asserts contiguity, returns the combined range.
- `intersection(other)` — returns `optional<DriverBitRange>` (nullopt when disjoint).

Callers that previously clipped bounds inline (`auto lo = max(...); auto hi = min(...); if (lo > hi) return;`) should use `intersection`.

### VariableTracker uses exact-bounds lookup

`VariableTracker::lookup(symbol, bounds)` matches on **exact** bounds, not overlap or containment. Port/Variable/State nodes are registered once at creation time with a specific `DriverBitRange` and can only be retrieved with that same range.

Consequences:
- Any site that walks a post-merge interval map and calls `getVariable(sym, intervalBounds)` can miss — the interval map may have split the original range into sub-intervals that were never registered. `hookupOutputPort` handles this by falling back to `getVariable(sym)` (no bounds) and picking the first registered node whose bounds *contain* the sub-interval.
- If you add a new site that looks up variable/port nodes by interval-map bounds, it needs the same fallback.

### Interval-map driver splits

`ValueTracker::addDrivers` inserts into an `IntervalMap` keyed by bit range. New drivers that overlap existing entries cause **splits**: the existing entry is erased and reinserted as one or more sub-intervals pointing to the same (or new) driver list.

Key property: a single driver *can* appear in multiple intervals of the same map, and **those intervals are not guaranteed to be adjacent**. Example:

```systemverilog
always_comb begin
  t[10:0] = a;  // A drives [0,10]
  t[3]    = b;  // B drives [3]; overwrites the middle of A's range
end
```

Result: `{[0,2]→A, [3,3]→B, [4,10]→A}`. A is split into non-adjacent intervals `[0,2]` and `[4,10]`. If downstream code re-emits edges per interval, the second A-emission will hit `setVariable`'s contiguity assertion.

No current test exercises this pattern. If/when it comes up, see the "Edge bounds" note above.

## Build pipeline

### Two-phase build (`NetlistBuilder::build`)

1. **Phase 1 (sequential)**: visit the entire elaborated AST to force lazy symbol construction and to register Port/Variable nodes in `VariableTracker`. Procedural and continuous-assign blocks are collected into `deferredBlocks` but not yet analysed.
2. **Phase 2 (parallel, optional)**: dispatch one task per deferred block to a thread pool. Each task runs `DataFlowAnalysis` and writes its intermediate results into a thread-local `DeferredGraphWork` buffer (`threadLocalDeferredWork`), *not* the shared graph. After all tasks complete, `drainDeferredWork` runs single-threaded and commits nodes, edges, pending R-values, and deferred merge lambdas to the shared `graph`/`driverMap`.

Invariants:

- `VariableTracker` inserts happen in Phase 1 only; Phase 2 only reads.
- `graph.addNode` / `graph.addEdge` is only called from the main thread (directly in Phase 1, or via `drainDeferredWork` after Phase 2). Any builder helper that mutates the graph must either be called on the main thread, or route through `threadLocalDeferredWork` when it exists. Check `if (threadLocalDeferredWork)` at every allocation site.
- `finalize()` (called after `build()`) runs `processPendingRvalues`, which emits edges for R-values that couldn't be resolved locally within a procedural block.

### Where edges get annotated

Sites that emit edges with `symbol` + `bounds` annotations — update this list when adding new ones:

- `DataFlowAnalysis::handleRvalue` → `addDriversToNode` → `addDependency` (local R-values resolved within a procedural block).
- `NetlistBuilder::processPendingRvalues` (R-values that escape their procedural block — resolved in `finalize`).
- `NetlistBuilder::hookupOutputPort` (driver → output-port edges, called per interval of the merged driver map).
- `NetlistBuilder::mergeDrivers` (clocked sequential branch creates a `State` node and emits driver → state edges; also the combinational interface/variable redirection at the bottom of the same function).

Every one of these funnels through `NetlistBuilder::addDependency`, which in turn calls `NetlistEdge::setVariable`. The union-on-collision / contiguity contract therefore applies uniformly.

## Testing

- Unit tests use the `NetlistTest` fixture in `tests/unit/Test.hpp`, which compiles inline SystemVerilog, runs `NetlistBuilder::build` + `finalize`, and exposes graph queries. Add a `parallel=true` variant when the behaviour under parallel Phase-2 could diverge from sequential (e.g. edge emission order, deferred merges, pending R-values).
- Driver-query tests live in `tests/unit/DriverTests.cpp` and should cover both internal variables (read via a downstream `assign`) and directly-written output ports. These paths use different edge-emission sites (`processPendingRvalues` vs `hookupOutputPort`) and historically regressed independently.
- When adding helpers on `DriverBitRange` / other small value types, add direct unit tests in `tests/unit/UtilityTests.cpp` alongside the behavioural integration tests — the unit tests catch boundary conditions (single-bit, abutting, disjoint) cheaply.

## Gotchas

- **Symbol reference identity.** `NetlistEdge::setVariable`'s collision check compares `hierarchicalPath` string equality. A source-target pair can legitimately carry edges for different symbols (e.g. a control-flow edge with empty symbol, later overwritten by a data-flow edge). Empty-path edges are treated as "not yet set" and will be fully replaced by the first real annotation.
- **Edge annotation in the dot renderer.** `NetlistDot` and the driver CLI print `symbol.name + toString(edge->bounds)` as the edge label. Tests in `NetlistTests.cpp` ("Edge annotation") compare this label verbatim, so changes to how edge bounds are computed can break the renderer test even when the graph is semantically correct.
- **`DirectedGraph::addEdge` returns an existing edge.** Do not assume a fresh edge on every call. If you need to detect "first annotation vs subsequent", check whether `edge.symbol.hierarchicalPath.empty()`.
- **`ConstantRange::intersect` returns an empty range on no overlap, not an optional.** Prefer `DriverBitRange::intersection` which returns `optional<DriverBitRange>`.
