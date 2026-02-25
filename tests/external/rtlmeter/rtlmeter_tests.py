"""
Tests that slang-netlist successfully builds a netlist for each RTLmeter design.
"""

import platform
import re
import subprocess
import sys
import tempfile
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


def make_design_test(rtlmeter_dir, design_name, design_dir, compile_section):
    """
    Return a unittest test method that runs slang-netlist on the design.
    """

    def test(self):
        args = build_args(rtlmeter_dir, design_dir, compile_section)
        argfile = self.tmpdir / f"{design_name}.f"
        write_argfile(args, argfile)
        cmd = [str(self.executable), "-f", str(argfile)]
        # Wrap with /usr/bin/time to capture peak RSS.
        if time_cmd := get_time_command():
            cmd = time_cmd + cmd
        start = time.perf_counter()
        print(f"{self.executable} -f {argfile}")
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        elapsed = time.perf_counter() - start
        peak_rss = parse_peak_rss(result.stderr) if time_cmd else None
        self.stats[design_name] = {"time": elapsed, "peak_rss": peak_rss}
        self.assertEqual(
            result.returncode,
            0,
            f"slang-netlist failed for {design_name}:\n{result.stderr}",
        )

    return test


class RtlmeterTests(unittest.TestCase):
    executable = None
    rtlmeter_dir = None
    tmpdir = None
    stats = {}


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


if __name__ == "__main__":
    if len(sys.argv) > 2:
        RtlmeterTests.executable = Path(sys.argv.pop(1))
        RtlmeterTests.rtlmeter_dir = Path(sys.argv.pop(1))
    # Remaining positional args (those not starting with '-') are design names.
    designs = []
    while len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        designs.append(sys.argv.pop(1))
    with tempfile.TemporaryDirectory(prefix="rtlmeter-") as tmpdir:
        RtlmeterTests.tmpdir = Path(tmpdir)
        add_design_tests(RtlmeterTests.rtlmeter_dir, designs)
        try:
            unittest.main(exit=False)
        finally:
            print_stats_table(RtlmeterTests.stats)
