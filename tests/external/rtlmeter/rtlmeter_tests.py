"""
Tests that slang-netlist successfully builds a netlist for each RTLmeter design.
"""

import argparse
import platform
import re
import shutil
import subprocess
import sys
import time
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
    args = [
        "-Wno-duplicate-definition",
        "-Wno-multiple-always-assigns",
        "-Wno-multi-write",
        "-DVERILATOR",
    ]

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


def write_argfile(args: list[str], path: Path) -> None:
    """
    Write command-line arguments to a .f file (one argument per line).
    """
    path.write_text("\n".join(args) + "\n")


def parse_peak_rss(time_stderr: str) -> Optional[int]:
    """
    Extract peak RSS in bytes from /usr/bin/time stderr output.

    On macOS, /usr/bin/time -l reports "maximum resident set size" in bytes.
    On Linux, /usr/bin/time -v reports "Maximum resident set size" in kbytes.
    """
    # macOS: "  1234567  maximum resident set size"
    m = re.search(r"(\d+)\s+maximum resident set size", time_stderr)
    if m:
        return int(m.group(1))
    # Linux: "Maximum resident set size (kbytes): 1234"
    m = re.search(r"Maximum resident set size.*?:\s*(\d+)", time_stderr)
    if m:
        return int(m.group(1)) * 1024
    return None


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


def get_time_command() -> Optional[list[str]]:
    """
    Detect the platform-appropriate /usr/bin/time flags.
    """
    time_path = Path("/usr/bin/time")
    if time_path.is_file():
        return (
            [str(time_path), "-l"]
            if platform.system() == "Darwin"
            else [str(time_path), "-v"]
        )
    return None


def run_once(
    executable: Path,
    argfile: Path,
    extra_args: Optional[list[str]] = None,
) -> tuple[float, Optional[int], int, str]:
    """
    Run slang-netlist once and return (elapsed, peak_rss, returncode, stderr).
    """
    cmd = [str(executable), "-f", str(argfile)]
    if extra_args:
        cmd.extend(extra_args)
    time_cmd = get_time_command()
    if time_cmd:
        cmd = time_cmd + cmd
    start = time.perf_counter()
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    elapsed = time.perf_counter() - start
    peak_rss = parse_peak_rss(result.stderr) if time_cmd else None
    return elapsed, peak_rss, result.returncode, result.stderr


def make_design_test(
    rtlmeter_dir: Path,
    design_name: str,
    design_dir: Path,
    compile_section: dict,
    extra_flags: Optional[list[str]] = None,
):
    """
    Return a unittest test method that runs slang-netlist on the design.

    In benchmark mode, runs the tool at multiple thread counts and records
    timing for each. In normal mode, runs once and asserts success.
    """

    def test(self):
        include_dir = self.tmpdir / f"{design_name}_inc"
        args = build_args(
            rtlmeter_dir, design_dir, compile_section, include_dir, extra_flags
        )
        argfile = self.tmpdir / f"{design_name}.f"
        write_argfile(args, argfile)

        if self.benchmark:
            bench_results = {}
            for tc in self.thread_counts:
                print(f"{self.executable} -f {argfile} --threads {tc}")
                elapsed, peak_rss, rc, stderr = run_once(
                    self.executable, argfile, ["--threads", str(tc)]
                )
                if rc != 0:
                    print(f"  {tc}T: FAIL")
                    bench_results[tc] = None
                else:
                    print(f"  {tc}T: {elapsed:.3f}s")
                    bench_results[tc] = {"time": elapsed, "peak_rss": peak_rss}
            self.bench_stats[design_name] = bench_results
        else:
            print(f"{self.executable} -f {argfile}")
            elapsed, peak_rss, rc, stderr = run_once(self.executable, argfile)
            self.stats[design_name] = {"time": elapsed, "peak_rss": peak_rss}
            self.assertEqual(
                rc, 0, f"slang-netlist failed for {design_name}:\n{stderr}"
            )

    return test


class RtlmeterTests(unittest.TestCase):
    executable: Optional[Path] = None
    rtlmeter_dir: Optional[Path] = None
    tmpdir: Optional[Path] = None
    stats: dict = {}
    benchmark: bool = False
    thread_counts: list[int] = [1, 2, 4, 8]
    bench_stats: dict = {}


def add_design_tests(
    rtlmeter_dir: Path,
    design_configs: Optional[dict[str, Optional[str]]] = None,
    design_extra_flags: Optional[dict[str, list[str]]] = None,
) -> None:
    """
    Discover RTLmeter designs and register a test method per design.

    If 'design_configs' is a non-empty dict, only the named designs are
    included.  Values are configuration names to apply on top of the base
    compile section (e.g. 'mini-chisel6' for XiangShan), or None to fall back
    to the descriptor's 'default' configuration when one exists.

    'design_extra_flags' is an optional dict mapping design names to lists of
    extra slang-netlist flags, used to work around issues specific to a design.
    """

    designs_dir = rtlmeter_dir / "designs"
    if not designs_dir.is_dir():
        return

    for design_path in sorted(designs_dir.iterdir()):
        design_name = design_path.name
        if design_configs is not None and design_name not in design_configs:
            continue
        descriptor_path = design_path / "descriptor.yaml"
        if not descriptor_path.is_file():
            continue

        with open(descriptor_path) as f:
            descriptor = yaml.safe_load(f)

        compile_section = descriptor.get("compile") or {}

        # Apply compile overrides from the explicitly specified configuration,
        # or from the 'default' configuration when none is specified.
        # These typically add defines or source files required for elaboration.
        config_name = (design_configs or {}).get(design_name)
        configs = descriptor.get("configurations") or {}
        chosen_compile = (configs.get(config_name or "default") or {}).get(
            "compile"
        ) or {}
        if chosen_compile:
            compile_section = merge_compile(compile_section, chosen_compile)

        if not compile_section.get("verilogSourceFiles"):
            continue

        extra_flags = (design_extra_flags or {}).get(design_name)
        method_name = "test_" + "".join(c if c.isalnum() else "_" for c in design_name)
        test_method = make_design_test(
            rtlmeter_dir, design_name, design_path, compile_section, extra_flags
        )
        test_method.__name__ = method_name
        setattr(RtlmeterTests, method_name, test_method)


def print_stats_table(stats: dict) -> None:
    """
    Print a summary table of per-design timing and memory statistics.
    """
    if not stats:
        return
    rows = []
    for name in sorted(stats):
        s = stats[name]
        rows.append([name, f"{s['time']:.3f}", format_bytes(s["peak_rss"])])
    total_time = sum(s["time"] for s in stats.values())
    rows.append(["Total", f"{total_time:.3f}", ""])
    print()
    print(
        tabulate(
            rows,
            headers=["Design", "Time (s)", "Peak RSS"],
            tablefmt="simple",
            colalign=("left", "right", "right"),
        )
    )


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
        baseline_time = baseline_result["time"] if baseline_result else None
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
                totals[t] += result["time"]
                row += _time_cols(result["time"], baseline_time)
        rows.append(row)

    baseline_total = totals[baseline]
    total_row = ["Total", f"{baseline_total:.3f}"]
    for t in thread_counts[1:]:
        total_row += _time_cols(totals[t], baseline_total or None)
    rows.append(total_row)

    print()
    print(tabulate(rows, headers=headers, tablefmt="simple", colalign=colalign))


def parse_design_spec(spec: str) -> tuple[str, Optional[str], list[str]]:
    """
    Parse a design spec string into (name, config, extra_flags).

    Accepted formats:
      DesignName
      DesignName:ConfigName
      DesignName:ConfigName:--flag1 --flag2

    The third colon-delimited segment is a space-separated list of extra
    slang-netlist flags; use the '=' form for flags with values
    (e.g. '--top=mymodule').  Leave the config segment empty to select the
    default configuration: 'DesignName::--flag'.
    """
    parts = spec.split(":", 2)
    name = parts[0]
    config = parts[1] if len(parts) > 1 else None
    flags = parts[2].split() if len(parts) > 2 and parts[2] else []
    return name, config or None, flags


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Run slang-netlist against RTLMeter designs.",
        epilog=(
            "Design specs: 'DesignName', 'DesignName:ConfigName', or "
            "'DesignName:ConfigName:--flag1 --flag2'. "
            "Leave ConfigName empty to use the default configuration."
        ),
    )
    parser.add_argument("executable", type=Path, help="Path to slang-netlist binary")
    parser.add_argument(
        "rtlmeter_dir", type=Path, help="Path to rtlmeter source directory"
    )
    parser.add_argument(
        "designs",
        nargs="*",
        metavar="DESIGN[:CONFIG[:FLAGS]]",
        help="Designs to test (default: all)",
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

    design_configs: dict[str, Optional[str]] = {}
    design_extra_flags: dict[str, list[str]] = {}
    for spec in args.designs:
        name, config, flags = parse_design_spec(spec)
        design_configs[name] = config
        if flags:
            design_extra_flags[name] = flags

    RtlmeterTests.executable = args.executable
    RtlmeterTests.rtlmeter_dir = args.rtlmeter_dir
    RtlmeterTests.tmpdir = Path.cwd()
    RtlmeterTests.benchmark = args.benchmark
    RtlmeterTests.thread_counts = args.threads
    add_design_tests(
        args.rtlmeter_dir, design_configs or None, design_extra_flags or None
    )
    sys.argv = [sys.argv[0]] + unittest_argv
    try:
        unittest.main(exit=False)
    finally:
        if RtlmeterTests.benchmark:
            print_benchmark_table(
                RtlmeterTests.bench_stats, RtlmeterTests.thread_counts
            )
        else:
            print_stats_table(RtlmeterTests.stats)
