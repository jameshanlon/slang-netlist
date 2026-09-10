#!/usr/bin/env python3
"""
Render charts from a benchmark summary CSV produced by summarise.py.

Design selection is driven by the data, so the charts work for whatever subset
of RTLMeter was run.
"""

import argparse
import csv
import json
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

THEMES = {
    # surface, primary ink, secondary ink, grid, four series, violet
    "light": ("#fcfcfb", "#0b0b0b", "#52514e", "#dcdcd6",
              ("#2a78d6", "#eb6834", "#1baf7a", "#eda100"), "#4a3aa7"),
    "dark": ("#212121", "#ffffff", "#c3c2b7", "#3d3d3a",
             ("#3987e5", "#d95926", "#199e70", "#c98500"), "#9085e9"),
}


def apply_theme(name: str):
    """
    Install a colour theme and return its palette.
    """
    surface, ink, ink2, grid, series, violet = THEMES[name]
    plt.rcParams.update(
        {
            "figure.facecolor": surface,
            "axes.facecolor": surface,
            "savefig.facecolor": surface,
            "font.size": 13,
            "text.color": ink,
            "axes.labelcolor": ink2,
            "xtick.color": ink2,
            "ytick.color": ink2,
            "axes.edgecolor": grid,
            "figure.dpi": 200,
            "legend.fontsize": 12,
        }
    )
    return surface, ink, ink2, grid, series, violet


def style(ax, grid):
    """
    Recessive grid, no top or right spine.
    """
    ax.grid(True, color=grid, linewidth=0.8, alpha=0.9)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def annotate(ax, rows, ycol, ink2, count=2, scale=1.0):
    """
    Label the smallest design and the @p count largest.

    @p scale converts the column's units to the plotted ones.
    """
    ordered = sorted(rows, key=lambda r: r["nodes"])
    picks = [(ordered[0], (10, 6))]
    for i, row in enumerate(ordered[-count:]):
        picks.append((row, (-10, 12) if i % 2 == 0 else (8, -16)))
    for row, offset in picks:
        ax.annotate(
            row["design"],
            (row["nodes"], row[ycol] * scale),
            textcoords="offset points",
            xytext=offset,
            color=ink2,
            fontsize=11,
            ha="left" if offset[0] > 0 else "right",
        )


def si(value: float) -> str:
    """
    Format a count as a short SI-style string, e.g. 1.5k or 16.6M.
    """
    for limit, suffix in ((1e6, "M"), (1e3, "k")):
        if value >= limit:
            return f"{value / limit:.1f}{suffix}"
    return f"{value:.0f}"


def load(csv_path: Path) -> list:
    """
    Read the summary CSV, converting every non-name column to float.
    """
    rows = list(csv.DictReader(open(csv_path)))
    for row in rows:
        for key, value in row.items():
            if key != "design" and value:
                row[key] = float(value)
    return rows


def chart_time(rows, out, palette, caption):
    surface, ink, ink2, grid, series, _ = palette
    nodes = [r["nodes"] for r in rows]
    fig, ax = plt.subplots(figsize=(8, 5))
    style(ax, grid)
    ax.scatter(nodes, [r["t1_total_s"] for r in rows], s=46, color=series[1],
               edgecolor=surface, linewidth=2, zorder=3, label="1 thread")
    ax.scatter(nodes, [r["t8_total_s"] for r in rows], s=46, color=series[0],
               edgecolor=surface, linewidth=2, zorder=4, label="8 threads")

    # Slope-1 reference anchored on the median design.
    anchor = sorted(rows, key=lambda r: r["nodes"])[len(rows) // 2]
    scale = anchor["t8_total_s"] / anchor["nodes"]
    span = [min(nodes), max(nodes)]
    ax.plot(span, [scale * x for x in span], color=ink2, linewidth=1.4,
            linestyle=(0, (5, 4)), alpha=0.6, zorder=2, label="linear reference")

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Netlist graph size (nodes)")
    ax.set_ylabel("Wall-clock time (s)")
    annotate(ax, rows, "t8_total_s", ink2)
    ax.legend(frameon=False, loc="upper left")
    fig.text(0.5, -0.02, caption, ha="center", color=ink2, fontsize=11)
    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)


def chart_memory(rows, out, palette, caption):
    surface, ink, ink2, grid, series, _ = palette
    nodes = [r["nodes"] for r in rows]
    fig, ax = plt.subplots(figsize=(8, 5))
    style(ax, grid)
    ax.scatter(nodes, [r["t8_peak_rss_mb"] / 1024 for r in rows], s=46,
               color=series[0], edgecolor=surface, linewidth=2, zorder=3)

    span = [min(nodes), max(nodes)]
    lx = [math.log(n) for n in nodes]
    ly = [math.log(r["t8_peak_rss_mb"] / 1024) for r in rows]
    n = len(lx)
    mx, my = sum(lx) / n, sum(ly) / n
    a = sum((x - mx) * (y - my) for x, y in zip(lx, ly)) / sum(
        (x - mx) ** 2 for x in lx)
    b = my - a * mx

    # A dashed guide is always the reference, so the fit is drawn solid.
    anchor = sorted(rows, key=lambda r: r["nodes"])[len(rows) // 2]
    slope1 = anchor["t8_peak_rss_mb"] / 1024 / anchor["nodes"]
    ax.plot(span, [slope1 * x for x in span], color=ink2, linewidth=1.4,
            linestyle=(0, (5, 4)), alpha=0.5, zorder=1,
            label="linear reference")
    ax.plot(span, [math.exp(b) * x**a for x in span], color=ink2,
            linewidth=1.4, alpha=0.75, zorder=2, label="power-law fit")

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Netlist graph size (nodes)")
    ax.set_ylabel("Peak resident memory (GiB)")
    ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f"{v:g}"))
    annotate(ax, rows, "t8_peak_rss_mb", ink2, scale=1 / 1024)
    handles, labels = ax.get_legend_handles_labels()
    ax.legend(handles[::-1], labels[::-1], frameon=False, loc="upper left")
    fig.text(0.5, -0.02, caption, ha="center", color=ink2, fontsize=11)
    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)


def chart_speedup(rows, out, palette, threads, chosen=None):
    """
    Netlist-construction speedup against thread count.

    Defaults to the four largest designs run at every thread count; @p chosen
    names designs explicitly instead.
    """
    surface, ink, ink2, grid, series, _ = palette
    keys = [f"netlist_speedup_{t}t" for t in threads[1:]]
    eligible = [r for r in rows if all(r.get(k) for k in keys)]
    if not eligible:
        return
    if chosen:
        by_name = {r["design"]: r for r in eligible}
        missing = [d for d in chosen if d not in by_name]
        if missing:
            raise SystemExit(f"no full thread sweep for: {', '.join(missing)}")
        picks = [by_name[d] for d in chosen]
    else:
        picks = sorted(eligible, key=lambda r: r["nodes"])[-4:]

    fig, ax = plt.subplots(figsize=(8, 5))
    style(ax, grid)
    (ideal,) = ax.plot(threads, threads, color=ink2, linewidth=1.4,
                     linestyle=(0, (5, 4)), alpha=0.6)
    # Colour stays tied to the design; only the legend is reordered, so that
    # it reads top to bottom like the curves themselves.
    curves = []
    for row, colour in zip(picks, series):
        ys = [1.0] + [row[k] for k in keys]
        (line,) = ax.plot(threads, ys, color=colour, linewidth=2, marker="o",
                        markersize=7, markeredgecolor=surface,
                        markeredgewidth=1.5)
        ax.annotate(f"{ys[-1]:.1f}x", (threads[-1], ys[-1]),
                    textcoords="offset points", xytext=(8, -3), color=colour,
                    fontsize=11, fontweight="bold")
        curves.append((ys[-1], line, row["design"]))
    curves.sort(key=lambda c: -c[0])

    ax.set_xscale("log", base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([f"{t}T" for t in threads])
    ax.set_xlim(threads[0] * 0.95, threads[-1] * 1.2)
    ax.set_xlabel("Worker threads")
    ax.set_ylabel("Speedup of netlist construction (x)")
    ax.legend([ideal] + [c[1] for c in curves],
              ["ideal"] + [c[2] for c in curves],
              frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)


def chart_phases(rows, out, palette, raw, reference):
    """
    Share of wall-clock time per phase for the six largest designs, ordered by
    total runtime.
    """
    surface, ink, ink2, grid, series, violet = palette
    picks = sorted(rows, key=lambda r: r["nodes"])[-6:]
    totals = {
        row["design"]: sum(
            raw[row["design"]][str(reference)]["time_seconds"].values())
        for row in picks
    }
    picks.sort(key=lambda r: totals[r["design"]])
    phases = [("elaboration", "Elaboration", series[1]),
              ("parsing", "Parsing", violet),
              ("analysis", "Analysis", series[2]),
              ("netlist", "Netlist build", series[0])]

    labels, shares = [], {key: [] for key, _, _ in phases}
    for row in picks:
        times = raw[row["design"]][str(reference)]["time_seconds"]
        total = totals[row["design"]]
        labels.append(f"{row['design']}  ({total:.0f}s)")
        for key, _, _ in phases:
            shares[key].append(100 * times[key] / total)

    fig, ax = plt.subplots(figsize=(8.5, 4.8))
    style(ax, grid)
    ax.grid(axis="y", visible=False)
    y = list(range(len(labels)))
    left = [0.0] * len(labels)
    for key, label, colour in phases:
        ax.barh(y, shares[key], left=left, color=colour, height=0.62, label=label)
        left = [l + s + 0.5 for l, s in zip(left, shares[key])]
    ax.set_yticks(y)
    ax.set_yticklabels(labels)
    ax.invert_yaxis()
    ax.set_xlabel(f"Share of wall-clock time at {reference} threads (%)")
    ax.set_xticks(range(0, 101, 20))
    ax.set_xlim(0, 101)
    ax.legend(frameon=False, loc="lower center", bbox_to_anchor=(0.5, -0.38), ncol=4)
    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="summary CSV from summarise.py")
    parser.add_argument("json", type=Path, nargs="+", help="benchmark JSON files")
    parser.add_argument("--theme", choices=sorted(THEMES), default="light")
    parser.add_argument("--outdir", type=Path, default=Path("."))
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 8])
    parser.add_argument("--caption", default="")
    parser.add_argument(
        "--speedup-designs",
        nargs="+",
        metavar="NAME",
        help="designs to plot in the speedup chart (default: four largest)",
    )
    args = parser.parse_args()

    palette = apply_theme(args.theme)
    rows = load(args.csv)
    raw = {}
    for path in args.json:
        raw.update(json.loads(path.read_text()))

    args.outdir.mkdir(parents=True, exist_ok=True)
    caption = args.caption or (
        f"{len(rows)} RTLMeter configurations, "
        f"{si(min(r['nodes'] for r in rows))}-{si(max(r['nodes'] for r in rows))} "
        "graph nodes"
    )
    chart_time(rows, args.outdir / "chart-time-vs-size.png", palette, caption)
    chart_memory(rows, args.outdir / "chart-memory-vs-size.png", palette,
                 caption)
    chart_speedup(rows, args.outdir / "chart-thread-speedup.png", palette,
                  args.threads, args.speedup_designs)
    chart_phases(rows, args.outdir / "chart-phase-share.png", palette, raw,
                 args.threads[-1])
    print(f"wrote 4 charts to {args.outdir}")


if __name__ == "__main__":
    main()
