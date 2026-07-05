# slang-netlist improvement tracker

Tracking ideas for extending the functionality of `slang-netlist` (and its
companion `slang-report`). Status legend: `[ ]` todo, `[~]` in progress,
`[x]` done. Add notes/PR links inline as work proceeds.

## 1. Expose existing graph queries on the CLI

The `NetlistGraph` API already computes these; they just need CLI wiring.

- [x] `--sensitivity <name>` — report the clocks/signals a node reacts to
      (backed by `getSensitivity`). Aggregates over all nodes sharing the
      name so a register's State node is reported even when a same-named
      output Port shadows it in `lookup`.
- [x] `--constant-drivers <name>` — surface tie-offs / stuck-at values
      (backed by `getConstantDrivers`).
- [ ] `--critical-path` / `--max-depth` — rank nodes by combinational logic
      depth from registers/inputs (reuse `PathFinder` + comb-edge filtering).
- [ ] Multi-path enumeration for `--from`/`--to` — report all paths, or the
      N shortest, instead of a single path.

## 2. Design-rule / lint-style checks

The bit-level driver tracking makes these cheap to compute.

- [ ] Undriven bits — report signal bit ranges with no driver.
- [ ] Multiply-driven bits — report bit ranges driven by more than one source.
- [ ] Unused / dangling nodes — outputs with no fan-in, nets with no fan-out.
- [ ] Clock-domain crossings — report edges where a value crosses between
      differently-clocked `State` nodes (uses `ast::EdgeKind` annotations).

## 3. Output & integration

- [ ] JSON output for `slang-netlist` query commands (`--fan-in`, `--fan-out`,
      `--find`, `--report-registers`), matching `slang-report`'s
      `--format=table|json`.
- [ ] Scoped DOT rendering — combine `--netlist-dot` with
      `--fan-in`/`--fan-out`/`--from`/`--to` to render only the relevant
      cone/path instead of the whole graph.
- [ ] `--scope`/`--name` glob filters (as in `slang-report`) to constrain
      query output to a module subtree.

## 4. Query ergonomics

- [ ] Single-endpoint traversal — allow `--from` (or `--to`) alone to mean
      "all reachable nodes".
- [ ] Check-oriented exit codes — `--comb-loops` and new lint checks should
      exit non-zero when a violation is found, so they work as CI gates.

## 5. Bit-precision reporting

- [ ] `--drivers <name>[bit-range]` — show, per bit, exactly which
      node/assignment drives it (exposes the bit-level granularity directly).

## Priority (value vs. effort)

1. Undriven / multiply-driven bit checks (§2) — new capability off existing data.
2. Critical-path / logic-depth report (§1) — new capability off existing data.
3. JSON output for query commands (§3) — unlocks CI use.
