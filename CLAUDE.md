# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Configure using a CMake preset (see `CMakePresets.json` for options; `CMakeUserPresets.json` for local overrides):

```sh
cmake --preset macos-local        # configure (macOS)
cmake --preset clang-debug        # configure (Linux, clang)
cmake --build build/macos-local   # build
```

Run all tests:

```sh
ctest --test-dir build/macos-local
```

Run a single unit test (Catch2 supports `-k` for filtering):

```sh
./build/macos-local/tests/unit/netlist_unittests -k "test name pattern"
```

Run only the Python driver tests:

```sh
ctest --test-dir build/macos-local -R python-driver-tests
```

## Code Style

- Follow [LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html) with these exceptions:
  - 80-column width
  - Functions, parameters, and local variables use lowerCamelCase (not UpperCase)
  - `#pragma once` instead of `#ifdef` guards
  - Exceptions are generally not permitted.
- Run `clang-format` with the project's local `.clang-format` settings before committing
- Install pre-commit hooks: `pip install pre-commit && pre-commit install`
- All code lives in the `slang::netlist` namespace

## Architecture

Slang Netlist is a C++ library that builds a **dependency graph** (the "netlist") over an elaborated SystemVerilog AST provided by [slang](https://sv-lang.com). The graph captures source-level static connectivity at bit-level granularity.

### Core Components

**Graph data structures** (`include/netlist/`):
- `DirectedGraph<NodeType, EdgeType>` — generic directed graph template
- `NetlistGraph` — specialization holding `NetlistNode`/`NetlistEdge`; the central artifact of the library
- `NetlistNode` — polymorphic base; concrete subtypes are `Port`, `Variable`, `Assignment`, `Conditional`, `Case`, `Merge`, `State`
- `NetlistEdge` — directed edge annotated with driven symbol, bit range, and `ast::EdgeKind` (clock sensitivity)

**Graph construction** (`source/`, `include/netlist/`):
- `NetlistBuilder` — main AST visitor (extends `slang::ast::ASTVisitor`) that traverses slang's elaborated AST and populates `NetlistGraph`. A two-phase approach: (1) visit all nodes to force lazy AST construction, then (2) traverse multithreadedly via slang's `AnalysisManager`
- `DataFlowAnalysis` — extends `slang::analysis::AbstractFlowAnalysis`; handles procedural blocks (always/initial) including if/case branching, loop unrolling, and non-blocking assignments
- `ValueTracker` / `VariableTracker` — interval-map-based structures that track which netlist nodes drive which bit ranges of each symbol
- `ExternalManager<T>` — handle-based allocator used because `IntervalMap` values must be trivially copyable

**Analysis and queries** (`include/netlist/`):
- `PathFinder` — DFS-based search between two `NetlistNode`s; returns a `NetlistPath`
- `CombLoops` / `CycleDetector` — detects combinational loops using edge-kind filtering (only traverses non-clocked edges)
- `DepthFirstSearch` — generic DFS template used by both `PathFinder` and `CycleDetector`

**Tooling**:
- `tools/driver/driver.cpp` — `slang-netlist` CLI binary (links against the `netlist` library)
- `bindings/python/pyslang_netlist.cpp` — pybind11 Python module (`pyslang_netlist`); enabled with `-DENABLE_PY_BINDINGS=ON`

### Testing Structure

- `tests/unit/` — Catch2 unit tests; `Test.hpp` provides the `NetlistTest` fixture (compiles inline SV text, runs analysis, exposes `pathExists`/`findPath`/`getDrivers`)
- `tests/driver/` — Python integration tests via `driver_tests.py` that invoke the `slang-netlist` CLI
- `tests/bindings/` — Python binding tests
- `tests/external/` — tests using SV code from external sources (enabled with `ENABLE_EXTERNAL_TESTS=ON`)

#### RTLMeter external tests (`tests/external/rtlmeter/`)

Fetches the [verilator/rtlmeter](https://github.com/verilator/rtlmeter) suite via CPM and runs `slang-netlist` against a curated list of real-world open-source designs (BlackParrot, Caliptra, NVDLA, OpenPiton, OpenTitan, VeeR-EH1/EH2/EL2, Vortex, XiangShan, XuanTie-C906/C910/E902/E906). Requires `pyyaml` and `tabulate` Python packages.

Run via ctest (60-minute timeout):

```sh
ctest --test-dir build/Local -R rtlmeter-tests
```

Or invoke the Python script directly for more control:

```sh
# Run a single design
python tests/external/rtlmeter/rtlmeter_tests.py \
    build/Local/tools/driver/slang-netlist \
    <rtlmeter-source-dir> \
    VeeR-EL2

# Run with explicit thread count
python tests/external/rtlmeter/rtlmeter_tests.py \
    build/Local/tools/driver/slang-netlist \
    <rtlmeter-source-dir> \
    --threads 4
```

After each run a summary table of per-design wall time and peak RSS is printed.

#### Thread-scalability benchmark (`bench-threads`)

The `bench-threads` CMake custom target measures netlist-build throughput at 1, 2, 4, and 8 threads for every design and prints a comparative table — useful for evaluating the parallel `AnalysisManager` path:

```sh
cmake --build build/Local --target bench-threads
```

Equivalent manual invocation (pass `--threads N1 N2 ...` to choose thread counts):

```sh
python tests/external/rtlmeter/rtlmeter_tests.py \
    build/Local/tools/driver/slang-netlist \
    <rtlmeter-source-dir> \
    --benchmark --threads 1 2 4 8
```

The benchmark table columns are labelled `1T`, `2T`, `4T`, `8T`; a `FAIL` cell means the tool exited non-zero at that thread count.

### Dependencies (fetched via CPM)

- `slang` — SystemVerilog compiler/AST/analysis (pinned to a specific git hash)
- `pybind11` — Python bindings
- `fmt` — string formatting
- `Catch2` — unit testing framework
