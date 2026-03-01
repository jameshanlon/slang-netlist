"""
Tests that slang-netlist successfully builds a netlist for each RTLmeter design.
"""

import platform
import re
import shutil
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


def build_args(rtlmeter_dir, design_dir, compile_section, include_dir):
    """
    Build a list of slang-netlist arguments from a compile descriptor section.

    include_dir: directory into which verilogIncludeFiles are copied so they
                 can be resolved by slang via -I.
    """
    args = [
        "--single-unit",
        "-Wno-duplicate-definition",
        "-Wno-multiple-always-assigns",
        "-Wno-multi-write",
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
        include_dir = self.tmpdir / f"{design_name}_inc"
        args = build_args(rtlmeter_dir, design_dir, compile_section, include_dir)
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


def add_design_tests(rtlmeter_dir, design_configs=None):
    """
    Discover RTLmeter designs and register a test method per design.

    If 'design_configs' is a non-empty dict, only the named designs are
    included.  Values are configuration names to apply on top of the base
    compile section (e.g. '1x1' for BlackParrot), or None to fall back to the
    descriptor's 'default' configuration when one exists.
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
    # Remaining positional args (those not starting with '-') are design specs
    # in 'DesignName' or 'DesignName:ConfigName' format.
    design_configs = {}
    while len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        spec = sys.argv.pop(1)
        name, _, config = spec.partition(":")
        design_configs[name] = config or None
    RtlmeterTests.tmpdir = Path.cwd()
    add_design_tests(RtlmeterTests.rtlmeter_dir, design_configs or None)
    try:
        unittest.main(exit=False)
    finally:
        print_stats_table(RtlmeterTests.stats)
