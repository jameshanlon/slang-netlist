# Implementation Plan: Bit-Precise Path Resolution Through Module Boundaries

## Goal

Make scalar→concat→port→concat→scalar paths bit-precise so that for

```sv
module x(input  logic [1:0] x, output logic [1:0] y);
  assign y = x;
endmodule
module m(input a, b, output c, d);
  x ux (.x({b,a}), .y({d,c}));
endmodule
```

`pathExists("m.a","m.d")` and `pathExists("m.b","m.c")` return false.

## Motivating netlist

For the test above, the current netlist is:

```
N1(a) ─┐
        ├→ N5 (ux.x) ──→ N7 (Assign y=x) ──→ N6 (ux.y) ─┬→ N3(c)
N2(b) ─┘                                                 └→ N4(d)
```

Three single-instance nodes lose precision: `N5`, `N7`, `N6`. Splitting any
one of them is not enough — paths still cross through whichever stays
whole-word.

### Why `N7` is single

`DataFlowAnalysis::handle(AssignmentExpression)`
(`source/DataFlowAnalysis.cpp:249`) builds `lhsList` from `y` and `rhsList`
from `x`. Both are simple LSPs of width 2, so `BitSliceList::pushLsp`
(`source/BitSliceList.cpp:238`) emits **one slice** each. `alignSegments`
returns one segment ⇒ one `createAssignment` call ⇒ one node. There's no
signal here that external concats want this assignment split.

### Why `N5` and `N6` are single

`NetlistBuilder::handle(PortSymbol)` (`source/NetlistBuilder.cpp:1001`)
iterates the analysis manager's drivers and emits one node per driver. An
input port has one driver covering the full range, so one node.
`buildPortSliceList` (line 846) builds a slicelist over per-node cut points,
but if there's only one node it has no internal cuts.

### Phase ordering

Inside `handle(InstanceSymbol)` for `ux`:

1. `symbol.body.visit(*this)` visits the body → `handle(PortSymbol)` creates
   the single `ux.x`/`ux.y` nodes; `assign y = x` is deferred to Phase 2.
2. `handlePortConnection` runs for `.x({b,a})` and `.y({d,c})` — this is
   where the concat cuts first become observable.

So at the moment we discover the cut points, the port nodes already exist
and the internal assignment hasn't been built yet.

## Strategy

Propagate cut points discovered at port connections *inward*, then
materialize port nodes and internal assignments at those cuts. No bit-aware
path traversal — keep `PathFinder` unchanged; rely on graph shape.

## Step 0 — Feature flag

Mirror the `resolveAssignBits` pattern.

### `BuilderOptions`

Add to `include/netlist/BuilderOptions.hpp`:

```cpp
/// Propagate concat-induced cut points across module port boundaries
/// so that scalar→concat→port→concat→scalar paths stay bit-precise.
/// When false, port nodes and module-internal assignments stay whole-
/// word at port boundaries (legacy behaviour). Default true.
bool propCutsAcrossPorts = true;
```

### CLI flag

Add `--no-prop-cuts-across-ports` to `tools/driver/driver.cpp` alongside
the existing `--no-resolve-assign-bits` flag, wired to set
`BuilderOptions::propCutsAcrossPorts = false`. Same wording style as the
existing flag's help text.

### Gating points

The flag short-circuits at three sites — when off, every step below
reverts to today's behaviour:

1. **Step 2 (cut-hint population) in `handlePortConnection`:** wrap the
   `cutHints.addCuts(...)` call in `if (options.propagateConcatCuts)`. If
   off, the registry stays empty and everything downstream becomes a
   no-op.
2. **Step 3 (`pushLsp` consultation):** the registry-aware split is
   naturally a no-op when the registry has no entries for the symbol, so
   no extra check needed — but for clarity, gate the registry pointer
   threaded into `BitSliceList::build` so callers explicitly pass `nullptr`
   when the flag is off.
3. **Step 4 (deferred port-node creation):** keep the deferred two-pass
   structure unconditionally (it's a refactor, not a behaviour change),
   but in `materializePortNodes` skip the cut-segment loop and create one
   node per driver (today's behaviour) when `options.propCutsAcrossPorts`
   is false.

### Interaction with `resolveAssignBits`

`propCutsAcrossPorts` requires `resolveAssignBits` — the cut hints have
nowhere to land if `BitSliceList::build` is forced into the opaque-only
mode. In `NetlistBuilder` startup (or `BuilderOptions` validation), if
`propCutsAcrossPorts && !resolveAssignBits`, log a warning and disable
`propCutsAcrossPorts`. Document this in the `BuilderOptions.hpp` comment.

## Step 1 — Cut-hint registry (load-bearing core)

**Where:** new `CutRegistry` owned by `NetlistBuilder`.

**Data:** `flat_hash_map<ast::ValueSymbol const*, std::set<uint64_t>>`. Cuts
are bit offsets within the symbol's selectable range (`0` and `width` are
implicit and not stored).

**API:**
- `addCuts(ValueSymbol const&, std::span<uint64_t const>)` — union into the
  existing set, thread-safe via mutex (Phase 1 is sequential today, but
  cheap to make safe up front).
- `cutsFor(ValueSymbol const&) -> std::span<uint64_t const>` — sorted,
  including the trivial 0/width endpoints when consumers want them.

**Invariant:** registry is read-only after Phase 1 ends. Phase 2 (DFA) is
the only consumer outside Phase 1's port-connection code.

**Files:** `source/CutRegistry.hpp` (new), `source/NetlistBuilder.{hpp,cpp}`.

## Step 2 — Populate cut hints from port connections

**Where:** `handlePortConnection` in `source/NetlistBuilder.cpp:742`.

**Logic:** after building `actualList` (line 787) and confirming widths
match (line 793), extract internal cut points from `actualList` (every
slice boundary that isn't 0 or width). If non-empty, look up the formal
port's *internal symbol* (`port.internalSymbol` cast to `ValueSymbol`) and
call `cutHints.addCuts(internalSymbol, cuts)`.

Symmetric for the *port side*: also collect cuts from `portList` (currently
driven by per-driver bounds — single-cut for unidirectional ports,
multi-cut for inout) and propagate them onto the actual's named LSP roots
if those roots are themselves formal ports of an enclosing instance. This
handles two-deep concat-through-port chains. **Defer this to Step 7** —
start with the inward direction only.

**No graph-shape change yet.** Verify with debug prints that hints are
registered for the test cases.

## Step 3 — `BitSliceList::pushLsp` consults the registry

**Where:** `source/BitSliceList.cpp:238`.

**Change:** signature gets an optional `CutRegistry const*` parameter
(threaded from `BitSliceList::build`, which gets it from the caller — DFA
passes its builder's registry; the port-connection path passes `nullptr`
to avoid recursion). When the LSP root's internal symbol has registered
cuts intersecting the LSP's bounds, emit multiple slices at those cuts
instead of one. Each slice still references the same `ValuePath` but with
narrowed `srcLo`/`srcHi`.

**Edge cases:**
- LSP that is a sub-range select: intersect cuts with the LSP's
  `lspBounds`, drop cuts at endpoints.
- Hierarchical LSP (`a.b.c`): use the leaf symbol for lookup, not the
  root, since hints are registered against the symbol the port maps to.

**Verification at this step:** internal `assign y = x` should now produce
two `Assignment` nodes (N7a, N7b). The graph still has single `N5`/`N6`
port nodes, so cross-bit paths still leak — but the structural change
inside the module is in place. Add a debug-only test that counts
Assignment nodes for the failing case.

## Step 4 — Defer port-node creation

**Problem:** `handle(PortSymbol)` (`source/NetlistBuilder.cpp:1001`) runs
*before* `handlePortConnection` for the enclosing instance, so cut hints
aren't visible yet.

**Restructure `handle(InstanceSymbol)` (line 1050):**

```
handle(InstanceSymbol):
  body.visit(*this)              // creates internal vars + DEFERS port-node creation
  for portConn in port connections:
    handlePortConnection(...)    // populates cut hints
  for portSymbol in body.ports:
    materializePortNodes(portSymbol)   // NEW — creates per-cut port nodes
```

**Change `handle(PortSymbol)` (line 1001):** instead of creating port
nodes, push the symbol onto a `pendingPorts` list on the builder.

**New `materializePortNodes(PortSymbol const&)`:**
- Look up cuts for the port's internal symbol via the registry.
- Compute the cut-aligned segments covering the port's selectable width.
- Iterate `analysisManager.getDrivers(valueSymbol)` (the existing
  per-driver loop) and for each driver, intersect its bounds with each
  cut segment; create one `Port` node per (driver, cut-segment) pair.
- For input drivers, call
  `addDriver(valueSymbol, nullptr, segmentBounds, &node)` per segment so
  the internal symbol is driven per-segment.

**Effect:** the test case now has `ux.x[0]`, `ux.x[1]`, `ux.y[0]`,
`ux.y[1]` as four port nodes.

## Step 5 — Adjust `buildPortSliceList`

**Where:** `source/NetlistBuilder.cpp:846`.

Already builds a slicelist over per-node cut points. Once
`handle(PortSymbol)` produces multiple nodes, `buildPortSliceList`
automatically picks up the cuts via `node->getBounds()`. No code change
expected — verify with a test.

## Step 6 — Hookup paths through `addDriver` / `addRvalue`

**Concern:** `addDriver` and `addRvalue` are keyed by `(symbol, bounds)`.
When a port's internal symbol now has multiple drivers (one per cut),
pre-existing call sites that drive the *full* internal-symbol range need
to still work — they should land on whichever sub-segment(s) overlap.

**Audit points:**
- `addDriver` in DFA when the LHS is a formal port's internal symbol.
- `hookupOutputPort` (`source/NetlistBuilder.cpp:609`) — already handles
  per-segment lookup via `getVariable(*portSymbol, bounds)` with a
  fallback. Verify the fallback isn't masking missed bit-precise
  hookups.
- `mergeDrivers` — should be unchanged; it's bit-range-aware via the
  interval map.

Most of this is already bit-range-aware; the change is that the interval
map will now have finer-grained entries.

## Step 7 — Hierarchical cut propagation (deferred from Step 2)

When a formal port is itself wired up as the actual side of an enclosing
instance's port connection, the inner cut hints need to propagate
*outward* too. Implement after Steps 1–6 work end-to-end on the
single-level test. Add a test like:

```sv
module inner(input [1:0] i, output [1:0] o); assign o = i; endmodule
module mid(input [1:0] mi, output [1:0] mo); inner u(.i(mi), .o(mo)); endmodule
module top(input a,b, output c,d);
  mid u(.mi({b,a}), .mo({d,c}));
endmodule
```

Two-pass propagation across the hierarchy until cut sets stabilize (worst
case O(depth × width)).

## Step 8 — Tests

**New unit tests in `tests/unit/ConcatTests.cpp`:**
- The single-level test (already added at line 247) — flip the
  `CHECK_FALSE` lines back to active.
- Two-level hierarchical test (Step 7).
- Concat with width-mismatched port (the existing fallback path should
  remain bit-imprecise but not crash).
- Multiple instances of the same module with different concat patterns —
  verifies that the cut-hint *union* doesn't lose precision for either
  instance.
- A wide signal (e.g. 64-bit) with concat at one boundary only —
  verifies cut hints stay bounded (only one extra split, not 64).
- One explicit "flag-off" test that constructs `NetlistTest` with
  `BuilderOptions{.resolveAssignBits = true, .propCutsAcrossPorts = false}`
  and asserts the cross-bit edges *do* exist — pinning the legacy
  behaviour so the flag's off-path doesn't regress unnoticed.

**Driver test:** the Python driver test suite picks up a
`--no-prop-cuts-across-ports` smoke test analogous to the existing
`--no-resolve-assign-bits` test.

**Regression check:** run the full suite. Tests that may be affected:
- `Concat: port connection sub u(.i({x,y}))` (currently around line 269)
  — the comment about "loose lower bound" should be revisited; this
  test may strengthen.
- All `tests/external/rtlmeter/` tests — check the bench-threads numbers
  don't regress materially due to extra Assignment nodes.

## Step 9 — Cost validation

Before merging, run `bench-threads` against rtlmeter and compare:
- Wall time delta per design.
- Peak RSS delta (more nodes = more memory).
- Edge count delta.

If any design shows >10% regression, profile and consider documenting the
flag (`--no-prop-cuts-across-ports`) as the escape hatch in user-facing
docs.

## Sequencing & checkpoints

| Step | Outcome | Verifiable how |
|------|---------|----------------|
| 0 | Flag plumbed end-to-end | Flag-off test pins legacy behaviour |
| 1 | Registry exists, no behaviour change | Unit test for registry add/lookup |
| 2 | Hints populated | Debug log shows hints for failing test |
| 3 | Internal assignment splits | Dot dump shows 2 Assignment nodes |
| 4 | Port nodes split | Dot dump shows 4 port nodes (2 per port) |
| 5 | (verification only) | No code change |
| 6 | Existing tests still pass | `ctest` green |
| 7 | Hierarchical case works | New test passes |
| 8 | Original failing test passes with `CHECK_FALSE` | Test reactivated |
| 9 | No perf regression | bench-threads diff |

## Risk register

- **Body sharing across instances:** mitigated by union semantics. Worst
  case is over-splitting, never wrong-splitting.
- **Inout ports:** already exercise per-driver port-node creation; should
  fall out cleanly but explicitly tested.
- **Phase-1 ordering:** assumes inner instances are visited before outer
  port connections. Verified by reading `handle(InstanceSymbol)` —
  `body.visit` then `handlePortConnection`. Hold this invariant in a
  comment.
- **Parallel Phase 2:** DFA blocks run in parallel and read the registry.
  Registry must be immutable across Phase 2 — enforce by a `freeze()`
  call between Phase 1 and Phase 2, with reads going through `cutsFor`
  only after freeze.
- **Pending RValues:** if any RValue queued in Phase 1 references a port
  node that gets split before Phase 4 resolves it, the resolution must
  pick the right sub-node. Easiest: don't queue port-node-bound RValues
  until after `materializePortNodes` runs (i.e. defer port-connection
  RValue queuing to after step 4 completes for that instance).
- **Default-on perf regression:** if Step 9's bench-threads pass shows a
  regression on any rtlmeter design, the flag gives a quick mitigation
  without a revert. Keep the flag-off path in CI for a release or two
  so any latent dependency on legacy semantics surfaces.

## Out of scope

- Bit-aware `PathFinder` (the alternative approach). Keeping this
  plan structural-graph-only.
- Per-bit decomposition of *opaque* operations (arithmetic, etc.) —
  those still fan whole-word.
- String / non-integral assignments — keep falling back to legacy walk.
