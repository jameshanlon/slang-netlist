#!/bin/bash
#
# Benchmark slang-netlist across different thread counts using RTLmeter
# designs. Requires a prior ctest run so that .f argfiles exist.
#
# Usage:
#   bench_threads.sh <slang-netlist> <argfile-dir> [threads...]
#
# Example:
#   bench_threads.sh ./build/macos-local/tools/driver/slang-netlist \
#                    build/macos-local/tests/external/rtlmeter 1 2 4 8

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <slang-netlist> <argfile-dir> [threads...]" >&2
    exit 1
fi

TOOL=$1; shift
ARGDIR=$1; shift

if [ $# -gt 0 ]; then
    THREADS=("$@")
else
    THREADS=(1 2 4 8)
fi

# Build the header.
printf "%-20s" "Design"
for t in "${THREADS[@]}"; do printf "%10s" "${t}T"; done
echo ""
printf "%-20s" "--------------------"
for t in "${THREADS[@]}"; do printf "%10s" "--------"; done
echo ""

# Run each design at each thread count.
for fpath in "$ARGDIR"/*.f; do
    design=$(basename "$fpath" .f)
    printf "%-20s" "$design"
    for t in "${THREADS[@]}"; do
        elapsed=$( { /usr/bin/time -p "$TOOL" -f "$fpath" \
            -Wno-duplicate-definition --threads "$t" \
            > /dev/null; } 2>&1 | grep ^real | awk '{print $2}')
        printf "%10s" "${elapsed}s"
    done
    echo ""
done
