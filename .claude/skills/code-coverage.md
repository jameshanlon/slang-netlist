---
name: code-coverage
description: Use when building with code coverage, generating coverage reports, or analyzing test coverage for slang-netlist. Triggers on coverage, profiling, llvm-cov, lcov, profdata, profraw.
---

# Code Coverage

Generate LLVM source-based code coverage reports for the netlist library.

## Prerequisites

- **Clang with compiler-rt profile runtime** (`libclang_rt.profile.a`). A source build of LLVM/Clang does NOT include this by default — you must build the `compiler-rt` project alongside clang. Without it, linking fails with `cannot find libclang_rt.profile.a`.
- `llvm-profdata` and `llvm-cov` from the same LLVM build.

## Quick Reference

| Step | Command |
|------|---------|
| Configure | `cmake -B build/clang-coverage -DCMAKE_CXX_COMPILER=<clang++> -DCODE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Release` |
| Build | `cmake --build build/clang-coverage --target netlist_unittests` |
| Full pipeline | `make -C build/clang-coverage coverage-lcov` |
| Terminal summary | `llvm-cov report <binary> --instr-profile=<profdata> --sources source/ include/` |

## Configure

Do NOT use `cmake --preset clang-coverage` — it hardcodes `clang++-21`. Instead configure directly:

```sh
cmake -G "Unix Makefiles" \
  -B build/clang-coverage \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/path/to/clang++ \
  -DCODE_COVERAGE=ON \
  -DENABLE_EXTERNAL_TESTS=OFF
```

CMake will `find_program()` for `llvm-profdata` and `llvm-cov`. If they are not on PATH, ensure the directory containing them is on PATH or set `CMAKE_PREFIX_PATH`.

## Build

```sh
cmake --build build/clang-coverage --target netlist_unittests -j$(nproc)
```

Both the `netlist` library and `netlist_unittests` binary are instrumented with `-fprofile-instr-generate -fcoverage-mapping`.

## Generate Coverage Report

Run the full pipeline (clean, run tests, merge profile, export LCOV):

```sh
make -C build/clang-coverage coverage-lcov
```

This chains four targets: `coverage-clean` -> `coverage-run` -> `coverage-merge` -> `coverage-lcov`.

## Output Files

| File | Path (relative to `build/clang-coverage/`) |
|------|---------------------------------------------|
| Raw profile | `netlist_unittests.profraw` |
| Merged profile | `netlist_unittests.profdata` |
| LCOV report | `coverage_output/coverage_report.lcov` |

## Viewing Results

**Terminal summary** (filtered to project sources only):

```sh
llvm-cov report \
  build/clang-coverage/tests/unit/netlist_unittests \
  --instr-profile=build/clang-coverage/netlist_unittests.profdata \
  --sources source/ include/
```

**HTML report** (if `genhtml` from lcov is installed):

```sh
genhtml build/clang-coverage/coverage_output/coverage_report.lcov \
  -o build/clang-coverage/coverage_output/html
```

## Common Mistakes

- **Using `cmake --preset clang-coverage`** — hardcodes compiler path that may not exist. Configure manually.
- **Missing compiler-rt** — LLVM source builds need `compiler-rt` built explicitly. The error is: `cannot find libclang_rt.profile.a`.
- **LCOV includes dependencies** — the `COVERAGE_IGNORE_REGEX` variable is empty by default, so Catch2/slang/fmt source appears in the report. Use `llvm-cov report --sources source/ include/` to filter to project code.
