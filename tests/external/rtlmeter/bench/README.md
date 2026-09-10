# RTLMeter benchmarks

Scripts for measuring `slang-netlist` build time, memory and thread scaling
across the RTLMeter designs, and for turning the results into charts.

## Build requirements

Benchmark from a release build **with the Python bindings disabled**:

```sh
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_PY_BINDINGS=OFF -DENABLE_EXTERNAL_TESTS=ON
cmake --build build/bench --target slang-netlist -j
```

Enabling the bindings drops slang's mimalloc integration, which costs about 30%
on this workload. `run-benchmarks.sh` warns if the binary it is given has no
mimalloc symbols; check by hand with:

```sh
nm -C build/bench/tools/driver/slang-netlist | grep -c mi_malloc   # expect > 0
```

`ENABLE_EXTERNAL_TESTS=ON` fetches RTLMeter itself; its source directory is then
`build/bench/_deps/rtlmeter-src`.

## Running

```sh
tests/external/rtlmeter/bench/run-benchmarks.sh \
    build/bench/tools/driver/slang-netlist \
    build/bench/_deps/rtlmeter-src \
    /path/to/scratch/dir
```

This sweeps every design at 1/2/4/8 threads, then writes `summary.csv` and two
sets of charts. Expect a couple of hours, dominated by the largest
configurations.

Every design needs all four thread counts to appear in the speedup chart, which
plots a curve rather than two endpoints. Cutting the large designs to `1 8`
halves their sweep time but drops them from that chart.

## Memory

The large configurations are demanding. OpenPiton 8x8 builds a 43 M node graph
and needs **more than 30 GB**; on a smaller host it swaps and the timings become
meaningless. Either run on a host with 40 GB or more, or drop it from the design
list in `run-benchmarks.sh`.

Peak RSS per design is reported in `summary.csv` (`t8_peak_rss_mb`).

## Pieces

- `run-benchmarks.sh` — end-to-end driver.
- `summarise.py` — merges the sweep JSON into one CSV and prints power-law fits
  of time and memory against graph size.
- `plots.py` — four charts: time vs size, memory vs size, thread speedup, and
  the per-phase share of wall-clock time. `--theme light|dark`; the dark theme
  is on `#212121` for dark slide decks. Design selection is data-driven, so a
  partial run still plots; `--speedup-designs` overrides the speedup chart's
  automatic pick of the four largest.

Both Python scripts are usable on their own, e.g. to re-plot without re-running:

```sh
python3 summarise.py bench-small.json bench-large.json -o summary.csv
python3 plots.py summary.csv bench-*.json --theme dark --outdir charts
```

Requires `pyyaml`, `tabulate` and `matplotlib`.
