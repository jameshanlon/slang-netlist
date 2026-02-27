"""
Tests that slang-netlist successfully builds a netlist for each RTLmeter design.
"""

import platform
import re
import subprocess
import sys
import time
import unittest
from pathlib import Path

import yaml
from tabulate import tabulate


def merge_compile(base, extra):
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


def build_args(rtlmeter_dir, design_dir, compile_section):
    """
    Build a list of slang-netlist arguments from a compile descriptor section.
    """
    args = []

    # Source files (paths relative to the design directory).
    args.append(str(rtlmeter_dir / "rtl" / "__rtlmeter_utils.sv"))
    for src in compile_section.get("verilogSourceFiles") or []:
        args.append(str(design_dir / src))

    # Include directories derived from include file parent paths.
    include_dirs = set()
    include_dirs.add(str(rtlmeter_dir / "rtl"))
    for inc in compile_section.get("verilogIncludeFiles") or []:
        include_dirs.add(str((design_dir / inc).parent))
    for inc_dir in sorted(include_dirs):
        args.extend(["-I", inc_dir])

    # Preprocessor defines.
    args.append(f"-D__RTLMETER_MAIN_CLOCK={compile_section.get('mainClock')}")
    for key, value in (compile_section.get("verilogDefines") or {}).items():
        args.append(f"-D{key}={value}")

    # Top module.
    top = compile_section.get("topModule")
    if top:
        args.extend(["--top", top])

    # Provide a default timescale for designs that mix timescaled and
    # non-timescaled files, and build the full netlist.
    args.extend(["--timescale", "1ns/1ps", "--report-registers", "-q"])

    return args


def write_argfile(args, path):
    """
    Write command-line arguments to a .f file (one argument per line).
    """
    path.write_text("\n".join(args) + "\n")


def parse_peak_rss(time_stderr):
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


def format_bytes(n):
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


def get_time_command():
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


def run_once(executable, argfile, extra_args=None):
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


def make_design_test(rtlmeter_dir, design_name, design_dir, compile_section):
    """
    Return a unittest test method that runs slang-netlist on the design.

    In benchmark mode, runs the tool at multiple thread counts and records
    timing for each. In normal mode, runs once and asserts success.
    """

    def test(self):
        tool_args = ["-Wno-duplicate-definition"]
        args = build_args(rtlmeter_dir, design_dir, compile_section)
        argfile = self.tmpdir / f"{design_name}.f"
        write_argfile(args, argfile)

        if self.benchmark:
            bench_results = {}
            for tc in self.thread_counts:
                print(f"{self.executable} -f {argfile} --threads {tc}")
                elapsed, peak_rss, rc, stderr = run_once(
                    self.executable,
                    argfile,
                    tool_args + ["--threads", str(tc)],
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
            elapsed, peak_rss, rc, stderr = run_once(
                self.executable,
                argfile,
                tool_args,
            )
            self.stats[design_name] = {"time": elapsed, "peak_rss": peak_rss}
            self.assertEqual(
                rc,
                0,
                f"slang-netlist failed for {design_name}:\n{stderr}",
            )

    return test


class RtlmeterTests(unittest.TestCase):
    executable = None
    rtlmeter_dir = None
    tmpdir = None
    stats = {}
    benchmark = False
    thread_counts = [1, 2, 4, 8]
    bench_stats = {}


def add_design_tests(rtlmeter_dir, designs=None):
    """
    Discover RTLmeter designs and register a test method per design.

    If 'designs' is a non-empty list, only the named designs are included.
    """

    designs_dir = rtlmeter_dir / "designs"
    if not designs_dir.is_dir():
        return

    for design_path in sorted(designs_dir.iterdir()):
        design_name = design_path.name
        if designs and design_name not in designs:
            continue
        descriptor_path = design_path / "descriptor.yaml"
        if not descriptor_path.is_file():
            continue

        with open(descriptor_path) as f:
            descriptor = yaml.safe_load(f)

        compile_section = descriptor.get("compile") or {}

        # Apply the 'default' configuration's compile overrides when present.
        # These typically add configuration-specific parameter include files
        # that are required for successful elaboration.
        configs = descriptor.get("configurations") or {}
        default_compile = (configs.get("default") or {}).get("compile") or {}
        if default_compile:
            compile_section = merge_compile(compile_section, default_compile)

        if not compile_section.get("verilogSourceFiles"):
            continue

        method_name = "test_" + "".join(c if c.isalnum() else "_" for c in design_name)
        test_method = make_design_test(
            rtlmeter_dir, design_name, design_path, compile_section
        )
        test_method.__name__ = method_name
        setattr(RtlmeterTests, method_name, test_method)


def print_stats_table(stats):
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


def print_benchmark_table(bench_stats, thread_counts):
    """
    Print a summary table with per-thread-count timing columns.
    """
    if not bench_stats:
        return
    headers = ["Design"] + [f"{t}T" for t in thread_counts]
    colalign = ("left",) + ("right",) * len(thread_counts)
    rows = []
    totals = {t: 0.0 for t in thread_counts}
    for name in sorted(bench_stats):
        row = [name]
        for t in thread_counts:
            result = bench_stats[name].get(t)
            if result is None:
                row.append("FAIL")
            else:
                row.append(f"{result['time']:.3f}")
                totals[t] += result["time"]
        rows.append(row)
    rows.append(["Total"] + [f"{totals[t]:.3f}" for t in thread_counts])
    print()
    print(tabulate(rows, headers=headers, tablefmt="simple", colalign=colalign))


if __name__ == "__main__":
    if len(sys.argv) > 2:
        RtlmeterTests.executable = Path(sys.argv.pop(1))
        RtlmeterTests.rtlmeter_dir = Path(sys.argv.pop(1))
    # Remaining positional args (those not starting with '-') are design names.
    designs = []
    while len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        designs.append(sys.argv.pop(1))
    # Parse --benchmark and --threads flags before unittest sees them.
    remaining = sys.argv[1:]
    sys.argv = sys.argv[:1]
    i = 0
    while i < len(remaining):
        if remaining[i] == "--benchmark":
            RtlmeterTests.benchmark = True
            i += 1
        elif remaining[i] == "--threads":
            i += 1
            thread_counts = []
            while i < len(remaining) and not remaining[i].startswith("-"):
                thread_counts.append(int(remaining[i]))
                i += 1
            RtlmeterTests.thread_counts = thread_counts
        else:
            sys.argv.append(remaining[i])
            i += 1
    RtlmeterTests.tmpdir = Path.cwd()
    add_design_tests(RtlmeterTests.rtlmeter_dir, designs)
    try:
        unittest.main(exit=False)
    finally:
        if RtlmeterTests.benchmark:
            print_benchmark_table(
                RtlmeterTests.bench_stats, RtlmeterTests.thread_counts
            )
        else:
            print_stats_table(RtlmeterTests.stats)
