"""
Tests that slang-netlist successfully builds a netlist for each RTLmeter design.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

import yaml
from tabulate import tabulate


def merge_compile(base: dict, extra: dict) -> dict:
    """
    Merge two compile sections.

    Lists are concatenated (extra appended to base), dicts are merged (extra
    overrides base), and scalar values are replaced by extra.
    """
    result = dict(base)
    for key, value in extra.items():
        if isinstance(value, list):
            result[key] = result.get(key, []) + value
        elif isinstance(value, dict):
            result[key] = {**result.get(key, {}), **value}
        else:
            result[key] = value
    return result


def load_design_configs(
    designs_yaml: Path,
    rtlmeter_dir: Path,
    names: Optional[list[str]] = None,
    size: str = "all",
) -> dict[str, tuple]:
    """
    Load the designs YAML and resolve each entry to (design_path,
    compile_section, extra_flags).

    YAML entries classify configurations into 'small_configs' and
    'large_configs' lists; 'size' selects which buckets are included
    ("small" = small_configs only, "all" = both).  A list entry of None
    (YAML ~) means "no config specifier" and produces a single test named
    after the design.

    Descriptors are read from <rtlmeter_dir>/designs/<Design>/descriptor.yaml
    and cached so multi-config designs only read once.  Designs whose
    descriptor is missing or whose resolved compile section has no sources
    are skipped.

    If 'names' is given, only those test names are included.
    """
    with open(designs_yaml) as f:
        raw = yaml.safe_load(f) or {}

    designs_dir = rtlmeter_dir / "designs"
    descriptors: dict[str, dict] = {}
    resolved: dict[str, tuple] = {}

    for design_name, entry in raw.items():
        entry = entry or {}
        if not entry.get("enabled", True):
            continue

        small = entry.get("small_configs") or []
        large = entry.get("large_configs") or []
        configs = list(small) if size == "small" else list(small) + list(large)
        if not configs:
            continue
        extra_flags = entry.get("args", [])

        for config in configs:
            test_name = f"{design_name}-{config}" if config else design_name
            if names is not None and test_name not in names:
                continue

            design_path = designs_dir / design_name
            if design_name not in descriptors:
                descriptor_path = design_path / "descriptor.yaml"
                descriptors[design_name] = (
                    yaml.safe_load(descriptor_path.read_text()) or {}
                    if descriptor_path.is_file()
                    else {}
                )
            descriptor = descriptors[design_name]
            if not descriptor:
                continue

            compile_section = descriptor.get("compile") or {}
            chosen = (
                (descriptor.get("configurations") or {}).get(config or "default") or {}
            ).get("compile")
            if chosen:
                compile_section = merge_compile(compile_section, chosen)

            if not compile_section.get("verilogSourceFiles"):
                continue

            resolved[test_name] = (design_path, compile_section, extra_flags)

    return resolved


def build_args(
    rtlmeter_dir: Path,
    design_dir: Path,
    compile_section: dict,
    include_dir: Path,
    extra_flags: Optional[list[str]] = None,
) -> list[str]:
    """
    Build a list of slang-netlist arguments from a compile descriptor section.

    include_dir: directory into which verilogIncludeFiles are copied so they
                 can be resolved by slang via -I.
    extra_flags: optional list of additional flags appended after all others,
                 used to work around issues specific to a design.
    """
    args = []

    # Top module.
    top = compile_section.get("topModule")
    if top:
        args.extend(["--top", top])

    # Copy include files into a flat directory and add it as an include path.
    # Also copy the rtlmeter top-level include so it is resolvable from the
    # same directory.
    include_files = compile_section.get("verilogIncludeFiles") or []
    include_dir.mkdir(exist_ok=True)
    shutil.copy(
        rtlmeter_dir / "rtl" / "__rtlmeter_top_include.vh",
        include_dir / "__rtlmeter_top_include.vh",
    )
    for inc in include_files:
        shutil.copy(design_dir / inc, include_dir / Path(inc).name)
    args.extend(["-I", str(include_dir)])

    # Source files (paths relative to the design directory).
    args.append(str(rtlmeter_dir / "rtl" / "__rtlmeter_utils.sv"))
    for src in compile_section.get("verilogSourceFiles") or []:
        args.append(str(design_dir / src))

    # Preprocessor defines.
    args.append(f"-D__RTLMETER_MAIN_CLOCK={compile_section.get('mainClock')}")
    for key, value in (compile_section.get("verilogDefines") or {}).items():
        args.append(f"-D{key}={value}")

    # Provide a default timescale for designs that mix timescaled and
    # non-timescaled files, and build the full netlist.
    args.extend(["--timescale", "1ns/1ps", "--report-registers", "-q"])

    # Append any per-design overrides last so they can override earlier flags.
    # Entries that don't start with '-' are treated as file paths relative to
    # the design directory (matching the slang_args.yaml convention).
    if extra_flags:
        for flag in extra_flags:
            if not flag.startswith("-"):
                args.append(str(design_dir / flag))
            else:
                args.append(flag)

    return args


def format_bytes(n: Optional[int]) -> str:
    """
    Format a byte count as a human-readable string.
    """
    if n is None:
        return "N/A"
    for unit in ("B", "KiB", "MiB", "GiB"):
        if abs(n) < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} TiB"


def parse_stats(stdout: str) -> Optional[dict]:
    """
    Extract the JSON stats object emitted by slang-netlist --stats-json.

    Returns the parsed dict or None if the stats line is not found.
    """
    for line in stdout.splitlines():
        line = line.strip()
        if line.startswith("{") and "time_seconds" in line:
            return json.loads(line)
    return None


def run_once(
    executable: Path,
    argfile: Path,
    extra_args: Optional[list[str]] = None,
) -> tuple[Optional[dict], int, str]:
    """
    Run slang-netlist once with --stats-json and return (stats, returncode, stderr).

    stats is the parsed JSON dict from --stats-json, or None on failure.
    """
    cmd = [str(executable), "-f", str(argfile), "--stats-json"]
    if extra_args:
        cmd.extend(extra_args)
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    stats = parse_stats(result.stdout) if result.returncode == 0 else None
    return stats, result.returncode, result.stderr


def run_design(
    test_name: str,
    executable: Path,
    rtlmeter_dir: Path,
    design_path: Path,
    compile_section: dict,
    extra_flags: Optional[list[str]],
    tmpdir: Path,
    benchmark: bool,
    thread_counts: list[int],
) -> tuple[bool, dict]:
    """
    Run slang-netlist for a single design.

    In benchmark mode the returned result maps thread count to a stats dict
    (or None on failure); otherwise it is a single stats dict (or None).
    """
    include_dir = tmpdir / f"{test_name}_inc"
    args = build_args(
        rtlmeter_dir, design_path, compile_section, include_dir, extra_flags
    )
    argfile = tmpdir / f"{test_name}.f"
    argfile.write_text("\n".join(args) + "\n")

    if benchmark:
        bench_results = {}
        success = True
        for tc in thread_counts:
            print(f"{executable} -f {argfile} --threads {tc}")
            run_stats, rc, stderr = run_once(
                executable, argfile, ["--threads", str(tc)]
            )
            if rc != 0 or run_stats is None:
                print(f"  {tc}T: FAIL")
                print(
                    f"slang-netlist failed for {test_name} at {tc}T "
                    f"(rc={rc}):\n{stderr}",
                    file=sys.stderr,
                )
                bench_results[tc] = None
                success = False
            else:
                print(f"  {tc}T: {_get_phase_time(run_stats, 'total'):.3f}s")
                bench_results[tc] = run_stats
        return success, bench_results

    print(f"{executable} -f {argfile}")
    run_stats, rc, stderr = run_once(executable, argfile)
    if rc != 0:
        print(f"slang-netlist failed for {test_name}:\n{stderr}", file=sys.stderr)
        return False, run_stats
    return True, run_stats


def _get_phase_time(stats: Optional[dict], phase: str) -> Optional[float]:
    """
    Extract a phase time from a stats dict.

    'phase' is either a top-level key in time_seconds (e.g. "elaboration")
    or a netlist_profile key (e.g. "phase2_parallel_seconds").  The special
    value "total" returns the sum of all top-level phases.
    """
    if stats is None:
        return None
    if phase == "total":
        return sum(stats["time_seconds"].values())
    ts = stats["time_seconds"].get(phase)
    if ts is not None:
        return ts
    profile = stats.get("netlist_profile")
    if profile is not None:
        return profile.get(phase)
    return None


# Phases shown in the summary table and available for per-phase benchmark
# breakdown.  Each entry is (display_name, stats_key).
TOP_PHASES = [
    ("Elaboration", "elaboration"),
    ("Analysis", "analysis"),
    ("Netlist", "netlist"),
]

NETLIST_SUB_PHASES = [
    ("P1 Collect", "phase1_collect_seconds"),
    ("P2 Parallel", "phase2_parallel_seconds"),
    ("P3 Drain", "phase3_drain_seconds"),
    ("P4 R-value", "phase4_rvalue_seconds"),
]

ALL_PHASES = TOP_PHASES + NETLIST_SUB_PHASES


def print_stats_table(stats: dict) -> None:
    """
    Print a summary table of per-design timing and memory statistics,
    including netlist sub-phase breakdown.
    """
    if not stats:
        return

    phase_keys = [key for _, key in ALL_PHASES]
    headers = ["Design"] + [label for label, _ in ALL_PHASES] + ["Total", "Peak RSS"]
    colalign = ("left",) + ("right",) * (len(headers) - 1)
    rows = []
    for name in sorted(stats):
        s = stats[name]
        if s is None:
            rows.append([name] + ["FAIL"] * (len(headers) - 1))
            continue
        total = _get_phase_time(s, "total")
        row = [name]
        for key in phase_keys:
            t = _get_phase_time(s, key)
            row.append(f"{t:.3f}" if t is not None else "")
        row += [f"{total:.3f}", format_bytes(s.get("peak_rss_bytes"))]
        rows.append(row)
    print()
    print(tabulate(rows, headers=headers, tablefmt="simple", colalign=colalign))


def _time_cols(t_time: float, baseline_time: Optional[float]) -> list:
    """Return [time_str, speedup_str] columns for a benchmark result."""
    if baseline_time is None:
        return [f"{t_time:.3f}", ""]
    return [f"{t_time:.3f}", f"{baseline_time / t_time:.2f}x"]


def _print_phase_benchmark_table(
    title: str,
    phase_key: str,
    bench_stats: dict,
    thread_counts: list[int],
) -> None:
    """
    Print a single benchmark table for one phase, with per-thread-count
    timing and speedup columns.
    """
    baseline = thread_counts[0]

    headers = ["Design", f"{baseline}T"]
    for t in thread_counts[1:]:
        headers += [f"{t}T", f"{t}T spd"]
    colalign = ("left",) + ("right",) * (len(headers) - 1)

    rows = []
    for name in sorted(bench_stats):
        baseline_result = bench_stats[name].get(baseline)
        baseline_time = _get_phase_time(baseline_result, phase_key)
        row = [name]
        if baseline_time is None:
            row.append("FAIL")
        else:
            row.append(f"{baseline_time:.3f}")
        for t in thread_counts[1:]:
            result = bench_stats[name].get(t)
            t_time = _get_phase_time(result, phase_key)
            if t_time is None:
                row += ["FAIL", ""]
            else:
                row += _time_cols(t_time, baseline_time)
        rows.append(row)

    print(f"\n{title}")
    print(tabulate(rows, headers=headers, tablefmt="simple", colalign=colalign))


def print_benchmark_table(bench_stats: dict, thread_counts: list[int]) -> None:
    """
    Print benchmark tables: an overall total table followed by per-phase
    breakdown tables showing timing and speedup at each thread count.
    """
    if not bench_stats:
        return

    _print_phase_benchmark_table("Total", "total", bench_stats, thread_counts)

    for label, key in ALL_PHASES:
        _print_phase_benchmark_table(label, key, bench_stats, thread_counts)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Run slang-netlist against RTLMeter designs.",
    )
    parser.add_argument("executable", type=Path, help="Path to slang-netlist binary")
    parser.add_argument(
        "rtlmeter_dir", type=Path, help="Path to rtlmeter source directory"
    )
    parser.add_argument(
        "--designs-yaml",
        type=Path,
        default=Path(__file__).parent / "designs.yaml",
        help="Path to designs YAML config (default: designs.yaml next to this script)",
    )
    parser.add_argument(
        "designs",
        nargs="*",
        metavar="DESIGN",
        help="Test names to run (default: all enabled in YAML)",
    )
    parser.add_argument(
        "--benchmark",
        action="store_true",
        help="Run each design at multiple thread counts",
    )
    parser.add_argument(
        "--threads",
        nargs="+",
        type=int,
        default=[1, 2, 4, 8],
        metavar="N",
        help="Thread counts to benchmark (default: 1 2 4 8)",
    )
    parser.add_argument(
        "--json-output",
        type=Path,
        default=None,
        metavar="FILE",
        help="Write combined per-design profiling stats to a JSON file",
    )
    parser.add_argument(
        "--size",
        choices=("small", "all"),
        default="all",
        help="Which design buckets to run: 'small' (small_configs only) or "
        "'all' (small_configs + large_configs). Default: all.",
    )
    args = parser.parse_args()

    resolved = load_design_configs(
        args.designs_yaml,
        args.rtlmeter_dir,
        args.designs or None,
        args.size,
    )

    tmpdir = Path.cwd()
    results: dict = {}
    failures: list[str] = []

    for test_name, (design_path, compile_section, extra_flags) in sorted(
        resolved.items()
    ):
        success, result = run_design(
            test_name,
            args.executable,
            args.rtlmeter_dir,
            design_path,
            compile_section,
            extra_flags,
            tmpdir,
            args.benchmark,
            args.threads,
        )
        results[test_name] = result
        if not success:
            failures.append(test_name)

    if args.benchmark:
        print_benchmark_table(results, args.threads)
    else:
        print_stats_table(results)

    if args.json_output:
        args.json_output.write_text(
            json.dumps(results, indent=2, sort_keys=True) + "\n"
        )
        print(f"\nWrote profiling stats to {args.json_output}")

    if failures:
        print(f"\nFAILED: {', '.join(failures)}", file=sys.stderr)
        sys.exit(1)
