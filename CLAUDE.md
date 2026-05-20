# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Configure using a CMake preset (see `CMakePresets.json` for options; `CMakeUserPresets.json` for local overrides):

```sh
cmake --preset macos-debug        # configure (macOS)
cmake --preset clang-debug        # configure (Linux, clang)
cmake --build build/macos-debug   # build
```

Python bindings are off by default (`option(ENABLE_PY_BINDINGS ... OFF)` in `CMakeLists.txt`); the `clang-debug`/`clang-release`/`gcc-debug`/`gcc-release`/`macos-debug`/`macos-release` presets turn them on. Pass `-DENABLE_PY_BINDINGS=ON` when configuring with any other preset.

Run all tests:

```sh
ctest --test-dir build/macos-debug
```

Run a single unit test (Catch2 supports `-k` for filtering):

```sh
./build/macos-debug/tests/unit/netlist_unittests -k "test name pattern"
```

Run only the Python driver tests:

```sh
ctest --test-dir build/macos-debug -R python-driver-tests
```

## Code Style

- Follow [LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html) with these exceptions:
  - 80-column width
  - Functions, parameters, and local variables use lowerCamelCase (not UpperCase)
  - `#pragma once` instead of `#ifdef` guards
  - Exceptions are generally not permitted.
- Run `clang-format` with the project's local `.clang-format` settings before committing.
- Install pre-commit hooks (`pip install pre-commit && pre-commit install`); the hook runs `clang-format` automatically.
- Library code lives in the `slang::netlist` namespace; the reporting visitors in `include/report/` live in `slang::report`.
- Keep comments short and concise. Do not add commentary that is related to the
  process of development. Prefer high level explanations rather than specific
  references to the code structure and naming.
- Always comment public API functions/methods and classes with an appropriate
  docstring or doxygen syntax.
- Format Python docstrings with the triple-quotes on separate lines.

## Workflow

- When changes have been completed, review the patch in detail to look for
  opportunities for simplification.
- Do not sign commits with `Co-authored-by: ...` trailers.

## Architecture

Slang Netlist is a C++ library that builds a **dependency graph** (the "netlist") over an elaborated SystemVerilog AST provided by [slang](https://sv-lang.com). The graph captures source-level static connectivity at bit-level granularity.

Generated documentation lives in `docs/`: `user-guide.dox` covers CLI usage, `developer-guide.dox` covers internals, and `mainpage.dox` is the Doxygen entry point.

### Core Components

**Graph data structures** (`include/netlist/`):
- `DirectedGraph<NodeType, EdgeType>` — generic directed graph template
- `NetlistGraph` — specialization holding `NetlistNode`/`NetlistEdge`; the central artifact of the library
- `NetlistNode` — polymorphic base; subtypes: `Port` (I/O), `Variable` (wire/reg), `State` (sequential persistent value), `Assignment`, `Conditional`, `Case`, `Merge` (branch join), `Constant` (literal value driver). Nodes represent operations or state
- `NetlistEdge` — directed edge (producer→consumer) annotated with driven symbol, bit range, and `ast::EdgeKind` (clock sensitivity). Edges represent data dependencies

**Graph construction** (`source/`, `include/netlist/`):
- `NetlistBuilder` — main AST visitor (extends `slang::ast::ASTVisitor`). Composed of `NodeFactory`, `PortConnectionHandler`, `PendingRvalueQueue`, `CanonicalBodyResolver`, and a `BuildPipeline` that owns the four-phase orchestration
- `BuildPipeline` — orchestrates the four-phase build: (1) sequential AST traversal to create ports/variables/instances and collect deferred DFA blocks, (2) DFA dispatch over deferred blocks (parallel when `options.parallel`), (3) drain thread-local pending R-values, (4) resolve pending R-values into graph edges (parallel when above threshold)
- `DataFlowAnalysis` — extends `slang::analysis::AbstractFlowAnalysis`; computes **reaching definitions** (which nodes last wrote each bit range), unlike slang's `DefaultDFA` which only tracks whether ranges are driven. Handles procedural blocks (always/initial) including if/case branching, loop unrolling, and non-blocking assignments
- `NodeFactory` — centralizes node allocation; registers each new node with the `NetlistGraph` and (for value-bearing kinds) records the (symbol, bounds) → node mapping in the builder's `VariableTracker`
- `PortConnectionHandler` — handles port-connection wiring; owns the slice allocator and `CutRegistry` that propagates concat-shaped actuals' bit boundaries down to formal ports
- `PendingRvalueQueue` — accumulates deferred R-values during Phase 2 (thread-local per task to avoid contention), then resolves them into edges in Phase 4
- `CanonicalBodyResolver` — redirects driver queries for non-canonical instance bodies to their canonical counterparts, since slang's `AnalysisManager` stores drivers only against canonical bodies
- `ValueTracker` / `VariableTracker` — interval-map-based structures that track which netlist nodes drive which bit ranges of each symbol
- `DriverMap` / `CutRegistry` — interval-map-keyed driver lookup and the per-symbol cut-point set used by the bit-aligned path
- `BitSlice` / `BitSliceList` — decomposition of an expression into contiguous bit slices with named sources; consumed by `alignSegments` for bit-aligned dependency resolution
- `ExternalManager<T>` — handle-based allocator used because `IntervalMap` values must be trivially copyable

**Common utilities** (`include/common/`):
- `Utilities` — table formatter and source-location stringifier shared by the netlist library and reporting tools
- `Wildcard` — glob-style matching (`*`, `**`/`...`, `?`) over `.`-separated hierarchical names; used by symbol-selection options and the `slang-report` `--scope` filter

**Analysis and queries** (`include/netlist/`):
- `PathFinder` — DFS-based search between two `NetlistNode`s; returns a `NetlistPath`
- `CombLoops` / `CycleDetector` — detects combinational loops using edge-kind filtering (only traverses non-clocked edges)
- `DepthFirstSearch` — generic DFS template used by both `PathFinder` and `CycleDetector`
- `NetlistSerializer` — JSON serialise/deserialise for a `NetlistGraph` (versioned format)
- `NetlistDot` — DOT-format renderer for visualizing a `NetlistGraph`

**Tooling**:
- `tools/driver/driver.cpp` — `slang-netlist` CLI binary (links against the `netlist` library)
- `tools/report/report.cpp` — `slang-report` CLI binary for inspecting an elaborated design; offers `--ports`, `--variables`, `--drivers`, and `--ast-json` modes. Uses the visitors in `include/report/` (`ReportPorts`, `ReportVariables`, `ReportDrivers`)
- `bindings/python/pyslang_netlist.cpp` — pybind11 Python module (`pyslang_netlist`); enabled with `-DENABLE_PY_BINDINGS=ON`

**Bit-aligned dependency resolution** (default-on, controlled by `BuilderOptions::resolveAssignBits` and the `--no-resolve-assign-bits` CLI flag): assignments and port connections are decomposed into a `BitSliceList` per side and zipped onto a common cut-point grid via `alignSegments`, so concatenations, replications, equal-width `?:`, and width-changing conversions (with zero/sign-extension padding) produce per-bit edges. Anything else — arithmetic, bitwise, relational, reductions, function calls, streaming concats, non-constant selects, narrowing conversions, pattern-bearing conditionals — is opaque, and every LSP inside fans into all bits of the slice (so `y = a & b` still records every bit of `a`,`b` driving every bit of `y`). Falls back to the legacy whole-expression LSP walk when either side is non-integral or the two slicelists disagree on width. See `docs/developer-guide.dox` for full internals documentation.

### Testing Structure

- `tests/unit/` — Catch2 unit tests; `Test.hpp` provides the `NetlistTest` fixture (compiles inline SV text, runs analysis, exposes `pathExists`/`findPath`/`getDrivers`)
- `tests/driver/` — Python integration tests via `driver_tests.py` that invoke the `slang-netlist` CLI
- `tests/report/` — Python integration tests via `report_tests.py` that invoke the `slang-report` CLI
- `tests/bindings/` — Python binding tests
- `tests/external/` — tests using SV code from external sources (enabled with `ENABLE_EXTERNAL_TESTS=ON`)

#### RTLMeter external tests (`tests/external/rtlmeter/`)

Fetches the [verilator/rtlmeter](https://github.com/verilator/rtlmeter) suite via CPM and runs `slang-netlist` against a curated list of real-world open-source designs (BlackParrot, Caliptra, NVDLA, OpenPiton, OpenTitan, Servant, VeeR-EH1/EH2/EL2, Vortex, XiangShan, XuanTie-C906/C910/E902/E906). Requires `pyyaml` and `tabulate` Python packages. Configure with `-DENABLE_EXTERNAL_TESTS=ON` (not set by any of the standard presets).

Run via ctest (60-minute timeout):

```sh
ctest --test-dir build/macos-debug -R rtlmeter-tests
```

Or invoke the Python script directly for more control:

```sh
# Run a single design
python tests/external/rtlmeter/rtlmeter_tests.py \
    build/macos-debug/tools/driver/slang-netlist \
    <rtlmeter-source-dir> \
    VeeR-EL2

# Run with explicit thread count
python tests/external/rtlmeter/rtlmeter_tests.py \
    build/macos-debug/tools/driver/slang-netlist \
    <rtlmeter-source-dir> \
    --threads 4
```

After each run a summary table of per-design wall time and peak RSS is printed.

#### Thread-scalability benchmark (`bench-threads`)

The `bench-threads` CMake custom target measures netlist-build throughput at 1, 2, 4, and 8 threads for every design and prints a comparative table — useful for evaluating the parallel `AnalysisManager` path:

```sh
cmake --build build/macos-debug --target bench-threads
```

Equivalent manual invocation (pass `--threads N1 N2 ...` to choose thread counts):

```sh
python tests/external/rtlmeter/rtlmeter_tests.py \
    build/macos-debug/tools/driver/slang-netlist \
    <rtlmeter-source-dir> \
    --benchmark --threads 1 2 4 8
```

The benchmark table columns are labelled `1T`, `2T`, `4T`, `8T`; a `FAIL` cell means the tool exited non-zero at that thread count.

### Dependencies (fetched via CPM)

- `slang` — SystemVerilog compiler/AST/analysis (pinned to a specific git hash)
- `pybind11` — Python bindings
- `fmt` — string formatting
- `Catch2` — unit testing framework
