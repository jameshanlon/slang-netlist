---
name: benchmarking
description: Use when benchmarking or profiling slang-netlist, measuring parallel scaling, or running RTLMeter designs by hand. Triggers on rtlmeter, bench-threads, thread scaling, profiling, perf, NVDLA, Vortex, XiangShan, peak RSS, argfile.
---

# Benchmarking

How to get real designs and what can be measured reliably on the 16-core / 30 GB dev host. For parallel-scaling internals and known bottlenecks see the `architecture` skill.

## Getting designs

RTLMeter design sources are committed directly in the verilator/rtlmeter repo (`designs/<Name>/src/`), not git submodules. A plain clone at the tag pinned in `tests/external/rtlmeter/CMakeLists.txt` gives working designs. The CPM checkout under `/tmp/rtlmeter` can be a broken partial (directories present, files missing); if so, re-clone.

To generate argfiles without running the whole suite, import `load_design_configs` and `build_args` from `tests/external/rtlmeter/rtlmeter_tests.py`.

The script needs `pyyaml` and `tabulate`. The host Python refuses system-wide installs (PEP 668), so use a venv.

## Build type

Measure with `clang-release`. The `gcc-release` preset actually sets `CMAKE_BUILD_TYPE=Debug`.

## Host limits

- No `perf` (`perf_event_paranoid=4`), and `ptrace_scope=1` stops gdb attaching to non-descendants. Use targeted in-code timers as the profiler.
- Vortex-huge (24-25 GB) and XiangShan-default are memory-bound and give unusable timings.
- NVDLA, XiangShan-mini-chisel3, XuanTie-C906 and OpenPiton-4x4 fit and reproduce the parallel-scaling behaviour.

## Running

Use `bench-threads` (or `rtlmeter_tests.py --benchmark --threads 1 2 4 8`) as described in the project CLAUDE.md. Edge counts vary slightly between 8-thread runs, so compare timings, not exact edge counts.
