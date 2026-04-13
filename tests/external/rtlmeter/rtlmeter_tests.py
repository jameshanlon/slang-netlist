"""
Tests that slang-netlist successfully builds a netlist for each RTLmeter design.
"""

import argparse
import json
import shutil
import subprocess
import sys
import unittest
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


def load_designs_yaml(
    path: Path,
    names: Optional[list[str]] = None,
) -> dict[str, dict]:
    """
    Load the designs YAML config and return a dict of enabled entries.

    Each returned value has keys: design, config, args.

    When a YAML entry has a 'configs' list, it is expanded into one test per
    config named "<Design>-<config>".

    If 'names' is given, only those test names are included (they must be
    present in the YAML after expansion).
    """
    with open(path) as f:
        raw = yaml.safe_load(f) or {}

    result: dict[str, dict] = {}
    for design_name, entry in raw.items():
        entry = entry or {}
        if not entry.get("enabled", True):
            continue

        configs = entry.get("configs")
        if configs is None:
            configs = [None]

        args = entry.get("args", [])

        for config in configs:
            if config is not None:
                test_name = f"{design_name}-{config}"
            else:
                test_name = design_name

            if names is not None and test_name not in names:
                continue

            result[test_name] = {
                "design": design_name,
                "config": config,
                "args": args,
            }
    return result


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
    Extract the JSON stats object emitted by slang-netlist --stats.

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
    Run slang-netlist once with --stats and return (stats, returncode, stderr).

    stats is the parsed JSON dict from --stats, or None on failure.
    """
    cmd = [str(executable), "-f", str(argfile), "--stats"]
    if extra_args:
        cmd.extend(extra_args)
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    stats = parse_stats(result.stdout) if result.returncode == 0 else None
    return stats, result.returncode, result.stderr


class RtlmeterTests(unittest.TestCase):
    executable: Optional[Path] = None
    rtlmeter_dir: Optional[Path] = None
    tmpdir: Optional[Path] = None
    stats: dict = {}
    benchmark: bool = False
    thread_counts: list[int] = [1, 2, 4, 8]
    bench_stats: dict = {}

    # Populated by add_design_tests(): test_name -> (design_path, compile, flags)
    design_configs: dict[str, tuple] = {}

    def _run_design(self, test_name: str) -> None:
        design_path, compile_section, extra_flags = self.design_configs[test_name]
        include_dir = self.tmpdir / f"{test_name}_inc"
        args = build_args(
            self.rtlmeter_dir,
            design_path,
            compile_section,
            include_dir,
            extra_flags,
        )
        argfile = self.tmpdir / f"{test_name}.f"
        argfile.write_text("\n".join(args) + "\n")

        if self.benchmark:
            bench_results = {}
            failures = []
            for tc in self.thread_counts:
                print(f"{self.executable} -f {argfile} --threads {tc}")
                run_stats, rc, stderr = run_once(
                    self.executable, argfile, ["--threads", str(tc)]
                )
                if rc != 0 or run_stats is None:
                    print(f"  {tc}T: FAIL")
                    bench_results[tc] = None
                    failures.append((tc, rc, stderr))
                else:
                    total = sum(run_stats["time_seconds"].values())
                    print(f"  {tc}T: {total:.3f}s")
                    bench_results[tc] = run_stats
            self.bench_stats[test_name] = bench_results
            if failures:
                msgs = [f"  {tc}T (rc={rc}):\n{stderr}" for tc, rc, stderr in failures]
                self.fail(f"slang-netlist failed for {test_name}:\n" + "\n".join(msgs))
        else:
            print(f"{self.executable} -f {argfile}")
            run_stats, rc, stderr = run_once(self.executable, argfile)
            self.stats[test_name] = run_stats
            self.assertEqual(rc, 0, f"slang-netlist failed for {test_name}:\n{stderr}")


def add_design_tests(
    rtlmeter_dir: Path,
    designs: dict[str, dict],
) -> None:
    """
    Read rtlmeter descriptors and register a test method per design.

    'designs' maps test names to dicts with keys: design, config, args.
    """
    designs_dir = rtlmeter_dir / "designs"
    if not designs_dir.is_dir():
        return

    # Cache parsed descriptors so multi-config designs only read once.
    descriptors: dict[str, dict] = {}

    for test_name, entry in sorted(designs.items()):
        design_name = entry["design"]
        design_path = designs_dir / design_name

        if design_name not in descriptors:
            descriptor_path = design_path / "descriptor.yaml"
            if not descriptor_path.is_file():
                descriptors[design_name] = {}
                continue
            with open(descriptor_path) as f:
                descriptors[design_name] = yaml.safe_load(f) or {}

        descriptor = descriptors[design_name]
        if not descriptor:
            continue

        compile_section = descriptor.get("compile") or {}

        # Apply compile overrides from the explicitly specified configuration,
        # or from the 'default' configuration when none is specified.
        config_name = entry.get("config")
        configs = descriptor.get("configurations") or {}
        chosen = (configs.get(config_name or "default") or {}).get("compile")
        if chosen:
            compile_section = merge_compile(compile_section, chosen)

        if not compile_section.get("verilogSourceFiles"):
            continue

        RtlmeterTests.design_configs[test_name] = (
            design_path,
            compile_section,
            entry.get("args") or None,
        )
        method_name = "test_" + "".join(c if c.isalnum() else "_" for c in test_name)

        def method(self, n=test_name):
            self._run_design(n)

        method.__name__ = method_name
        setattr(RtlmeterTests, method_name, method)


def _total_time(s: Optional[dict]) -> Optional[float]:
    """Sum the phase times from a stats dict, or return None."""
    if s is None:
        return None
    return sum(s["time_seconds"].values())


def print_stats_table(stats: dict) -> None:
    """
    Print a summary table of per-design timing and memory statistics.
    """
    if not stats:
        return
    phases = ["elaboration", "analysis", "netlist"]
    headers = ["Design"] + [p.capitalize() for p in phases] + ["Total", "Peak RSS"]
    colalign = ("left",) + ("right",) * (len(headers) - 1)
    rows = []
    ok_stats = {n: s for n, s in stats.items() if s is not None}
    for name in sorted(stats):
        s = stats[name]
        if s is None:
            rows.append([name] + ["FAIL"] * (len(headers) - 1))
            continue
        times = s["time_seconds"]
        total = sum(times.values())
        row = [name]
        row += [f"{times.get(p, 0):.3f}" for p in phases]
        row += [f"{total:.3f}", format_bytes(s.get("peak_rss_bytes"))]
        rows.append(row)
    totals = ["Total"]
    for p in phases:
        totals.append(
            f"{sum(s['time_seconds'].get(p, 0) for s in ok_stats.values()):.3f}"
        )
    totals += [f"{sum(_total_time(s) for s in ok_stats.values()):.3f}", ""]
    rows.append(totals)
    print()
    print(tabulate(rows, headers=headers, tablefmt="simple", colalign=colalign))


def _time_cols(t_time: float, baseline_time: Optional[float]) -> list:
    """Return [time_str, speedup_str] columns for a benchmark result."""
    if baseline_time is None:
        return [f"{t_time:.3f}", ""]
    return [f"{t_time:.3f}", f"{baseline_time / t_time:.2f}x"]


def print_benchmark_table(bench_stats: dict, thread_counts: list[int]) -> None:
    """
    Print a summary table with per-thread-count timing and speedup columns.

    Speedup is relative to the first thread count (baseline) and shown in a
    dedicated column for each non-baseline thread count.
    """
    if not bench_stats:
        return
    baseline = thread_counts[0]

    # Build headers: "1T", "2T", "2T spd", "4T", "4T spd", ...
    headers = ["Design", f"{baseline}T"]
    for t in thread_counts[1:]:
        headers += [f"{t}T", f"{t}T spd"]
    colalign = ("left",) + ("right",) * (len(headers) - 1)

    rows = []
    totals = {t: 0.0 for t in thread_counts}
    for name in sorted(bench_stats):
        baseline_result = bench_stats[name].get(baseline)
        baseline_time = _total_time(baseline_result)
        row = [name]
        if baseline_result is None:
            row.append("FAIL")
        else:
            totals[baseline] += baseline_time
            row.append(f"{baseline_time:.3f}")
        for t in thread_counts[1:]:
            result = bench_stats[name].get(t)
            if result is None:
                row += ["FAIL", ""]
            else:
                t_time = _total_time(result)
                totals[t] += t_time
                row += _time_cols(t_time, baseline_time)
        rows.append(row)

    baseline_total = totals[baseline]
    total_row = ["Total", f"{baseline_total:.3f}"]
    for t in thread_counts[1:]:
        total_row += _time_cols(totals[t], baseline_total or None)
    rows.append(total_row)

    print()
    print(tabulate(rows, headers=headers, tablefmt="simple", colalign=colalign))


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
    args, unittest_argv = parser.parse_known_args()

    designs = load_designs_yaml(args.designs_yaml, args.designs or None)

    RtlmeterTests.executable = args.executable
    RtlmeterTests.rtlmeter_dir = args.rtlmeter_dir
    RtlmeterTests.tmpdir = Path.cwd()
    RtlmeterTests.benchmark = args.benchmark
    RtlmeterTests.thread_counts = args.threads
    add_design_tests(args.rtlmeter_dir, designs)
    sys.argv = [sys.argv[0]] + unittest_argv
    try:
        result = unittest.main(exit=False)
    finally:
        if RtlmeterTests.benchmark:
            print_benchmark_table(
                RtlmeterTests.bench_stats, RtlmeterTests.thread_counts
            )
        else:
            print_stats_table(RtlmeterTests.stats)
    if not result.result.wasSuccessful():
        sys.exit(1)
