#!/usr/bin/env python3
"""
Summarise RTLMeter benchmark runs into a single table.

Reads the JSON files written by ``rtlmeter_tests.py --json-output`` (one per
sweep) and writes a CSV with one row per design, plus derived throughput and
scaling figures. Designs missing either thread count are skipped.
"""

import argparse
import csv
import json
import math
from pathlib import Path

SRC_EXT = (".sv", ".v", ".svh", ".vh")


def source_lines(name: str, argfile_dir: Path) -> int | None:
    """
    Count non-blank lines of the RTL listed in a design's generated .f file.
    """
    argfile = argfile_dir / f"{name}.f"
    if not argfile.is_file():
        return None
    total = 0
    for line in argfile.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("-"):
            continue
        path = Path(line)
        if path.suffix in SRC_EXT and path.is_file():
            total += sum(
                1
                for text in path.read_text(errors="ignore").splitlines()
                if text.strip()
            )
    return total


def total_time(stats: dict) -> float:
    """
    Total wall-clock time across all phases of one run.
    """
    return sum(stats["time_seconds"].values())


def build_row(name: str, per_thread: dict, argfile_dir: Path, baseline: int,
              reference: int) -> dict:
    """
    Build one summary row from a design's per-thread-count results.
    """
    base, ref = per_thread[baseline], per_thread[reference]
    profile = base["netlist_profile"]
    row = {
        "design": name,
        "sloc": source_lines(name, argfile_dir),
        "nodes": ref["graph_nodes"],
        "edges": ref["graph_edges"],
        "blocks": profile["deferred_block_count"],
        "rvalues": profile["deferred_pending_rvalue_count"],
        "t1_total_s": round(total_time(base), 3),
        "t8_total_s": round(total_time(ref), 3),
        "total_speedup_8t": round(total_time(base) / total_time(ref), 2),
        "t1_netlist_s": round(base["time_seconds"]["netlist"], 3),
        "t8_netlist_s": round(ref["time_seconds"]["netlist"], 3),
        "netlist_speedup_8t": round(
            base["time_seconds"]["netlist"] / ref["time_seconds"]["netlist"], 2
        ),
        "t1_dfa_s": round(profile["phase2_parallel_seconds"], 3),
        "t8_dfa_s": round(ref["netlist_profile"]["phase2_parallel_seconds"], 3),
        "dfa_speedup_8t": round(
            profile["phase2_parallel_seconds"]
            / ref["netlist_profile"]["phase2_parallel_seconds"],
            2,
        ),
        "t1_peak_rss_mb": round(base["peak_rss_bytes"] / 2**20, 1),
        "t8_peak_rss_mb": round(ref["peak_rss_bytes"] / 2**20, 1),
    }

    # Intermediate thread counts are only present in the small-design sweep.
    for threads in sorted(per_thread):
        if threads in (baseline, reference):
            continue
        run = per_thread[threads]
        row[f"total_speedup_{threads}t"] = round(
            total_time(base) / total_time(run), 2
        )
        row[f"netlist_speedup_{threads}t"] = round(
            base["time_seconds"]["netlist"] / run["time_seconds"]["netlist"], 2
        )
        row[f"t{threads}_peak_rss_mb"] = round(run["peak_rss_bytes"] / 2**20, 1)

    row["blocks_per_s_8t"] = round(row["blocks"] / row["t8_total_s"], 1)
    row["nodes_per_s_8t"] = round(row["nodes"] / row["t8_total_s"], 1)
    row["bytes_per_node_8t"] = round(ref["peak_rss_bytes"] / row["nodes"], 1)
    row["edges_per_node"] = round(row["edges"] / row["nodes"], 2)
    return row


def power_fit(xs: list, ys: list) -> tuple[float, float]:
    """
    Least-squares fit of log(y) = a*log(x) + b; returns the exponent and r^2.
    """
    lx = [math.log(x) for x in xs]
    ly = [math.log(y) for y in ys]
    n = len(lx)
    mx, my = sum(lx) / n, sum(ly) / n
    a = sum((x - mx) * (y - my) for x, y in zip(lx, ly)) / sum(
        (x - mx) ** 2 for x in lx
    )
    b = my - a * mx
    ss_res = sum((y - (a * x + b)) ** 2 for x, y in zip(lx, ly))
    ss_tot = sum((y - my) ** 2 for y in ly)
    return a, 1 - ss_res / ss_tot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json", type=Path, nargs="+", help="benchmark JSON files")
    parser.add_argument("-o", "--output", type=Path, default=Path("summary.csv"))
    parser.add_argument(
        "--argfile-dir",
        type=Path,
        default=Path("."),
        help="directory holding the generated .f files (for line counts)",
    )
    parser.add_argument("--baseline", type=int, default=1)
    parser.add_argument("--reference", type=int, default=8)
    args = parser.parse_args()

    rows = []
    for path in args.json:
        for name, per_thread in sorted(json.loads(path.read_text()).items()):
            if name.startswith("Example-"):
                continue
            runs = {int(k): v for k, v in per_thread.items() if v}
            if args.baseline not in runs or args.reference not in runs:
                print(f"skipping {name}: needs both thread counts")
                continue
            rows.append(
                build_row(name, runs, args.argfile_dir, args.baseline, args.reference)
            )

    if not rows:
        raise SystemExit("no designs to summarise")

    fields = []
    for row in rows:
        for key in row:
            if key not in fields:
                fields.append(key)
    with open(args.output, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {args.output} ({len(rows)} designs)")

    nodes = [r["nodes"] for r in rows]
    print("\nscaling vs graph nodes (y = c * nodes^a)")
    for label, key in (
        (f"time @{args.reference}T", "t8_total_s"),
        (f"time @{args.baseline}T", "t1_total_s"),
        (f"peak RSS @{args.reference}T", "t8_peak_rss_mb"),
    ):
        a, r2 = power_fit(nodes, [r[key] for r in rows])
        print(f"  {label:<18} exponent={a:.2f}  r2={r2:.3f}")

    rates = sorted(r["nodes_per_s_8t"] for r in rows)
    print(
        f"\nnodes: {min(nodes):,} .. {max(nodes):,}   "
        f"build rate @{args.reference}T: median {rates[len(rates) // 2]:,.0f} nodes/s"
    )


if __name__ == "__main__":
    main()
