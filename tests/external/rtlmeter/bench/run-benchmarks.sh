#!/usr/bin/env bash
#
# Run the RTLMeter benchmark sweeps and produce a summary and charts.
#
# Usage: run-benchmarks.sh <slang-netlist> <rtlmeter-source-dir> [outdir]
#
# Sweeps every design at 1/2/4/8 threads, then writes summary.csv and four
# charts. Run from a scratch directory: the sweeps drop a .f file per design
# into the working directory.

set -euo pipefail

if [[ $# -lt 2 ]]; then
    sed -n '3,10p' "$0" >&2
    exit 1
fi

executable=$(realpath "$1")
rtlmeter=$(realpath "$2")
outdir=$(realpath "${3:-.}")
here=$(cd "$(dirname "$0")" && pwd)
tests="$here/.."

if ! nm -C "$executable" 2>/dev/null | grep -q mi_malloc; then
    echo "warning: $executable has no mimalloc symbols." >&2
    echo "         Build with -DENABLE_PY_BINDINGS=OFF; enabling the Python" >&2
    echo "         bindings drops mimalloc and costs roughly 30%." >&2
fi

mkdir -p "$outdir"
cd "$outdir"

echo "== small designs, 1/2/4/8 threads =="
python3 "$tests/rtlmeter_tests.py" "$executable" "$rtlmeter" \
    --designs-yaml "$tests/designs.yaml" \
    --benchmark --threads 1 2 4 8 --size small \
    --timeout 1800 --json-output bench-small.json

echo "== large designs, 1/2/4/8 threads =="
python3 "$tests/rtlmeter_tests.py" "$executable" "$rtlmeter" \
    --designs-yaml "$tests/designs.yaml" \
    --benchmark --threads 1 2 4 8 --size all \
    --timeout 7200 --json-output bench-large.json \
    OpenPiton-2x2 OpenPiton-4x4 OpenPiton-8x8 Vortex-huge Vortex-sane \
    XiangShan-default-chisel3 XiangShan-default-chisel6 XuanTie-C910

python3 "$here/summarise.py" bench-small.json bench-large.json -o summary.csv
python3 "$here/plots.py" summary.csv bench-small.json bench-large.json \
    --theme light --outdir charts-light
python3 "$here/plots.py" summary.csv bench-small.json bench-large.json \
    --theme dark --font-scale 1.25 --outdir charts-dark

echo
echo "results in $outdir: summary.csv, charts-light/, charts-dark/"
