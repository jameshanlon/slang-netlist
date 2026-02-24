"""
Tests that slang-netlist successfully builds a netlist for each RTLmeter design.
"""

import os
import subprocess
import sys
import unittest

import yaml


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


def build_command(executable, design_dir, compile_section):
    """
    Build a slang-netlist invocation from a compile descriptor section.
    """
    cmd = [executable]

    # Source files (paths relative to the design directory).
    for src in compile_section.get("verilogSourceFiles") or []:
        cmd.append(os.path.join(design_dir, src))

    # Include directories derived from include file parent paths.
    include_dirs = set()
    for inc in compile_section.get("verilogIncludeFiles") or []:
        include_dirs.add(os.path.dirname(os.path.join(design_dir, inc)))
    for inc_dir in sorted(include_dirs):
        cmd.extend(["-I", inc_dir])

    # Preprocessor defines.
    for key, value in (compile_section.get("verilogDefines") or {}).items():
        cmd.append(f"-D{key}={value}")

    # Top module.
    top = compile_section.get("topModule")
    if top:
        cmd.extend(["--top", top])

    # Provide a default timescale for designs that mix timescaled and
    # non-timescaled files, and build the full netlist.
    cmd.extend(["--timescale", "1ns/1ps", "--report-registers", "-q"])

    return cmd


def make_design_test(design_name, design_dir, compile_section):
    """
    Return a unittest test method that runs slang-netlist on the design.
    """

    def test(self):
        cmd = build_command(self.executable, design_dir, compile_section)
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        self.assertEqual(
            result.returncode,
            0,
            f"slang-netlist failed for {design_name}:\n{result.stderr}",
        )

    return test


class RtlmeterTests(unittest.TestCase):
    executable = None
    rtlmeter_dir = None


def add_design_tests(rtlmeter_dir, designs=None):
    """
    Discover RTLmeter designs and register a test method per design.

    If 'designs' is a non-empty list, only the named designs are included.
    """

    designs_dir = os.path.join(rtlmeter_dir, "designs")
    if not os.path.isdir(designs_dir):
        return

    for design_name in sorted(os.listdir(designs_dir)):
        if designs and design_name not in designs:
            continue
        design_dir = os.path.join(designs_dir, design_name)
        descriptor_path = os.path.join(design_dir, "descriptor.yaml")
        if not os.path.isfile(descriptor_path):
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
        test_method = make_design_test(design_name, design_dir, compile_section)
        test_method.__name__ = method_name
        setattr(RtlmeterTests, method_name, test_method)


if __name__ == "__main__":
    if len(sys.argv) > 2:
        RtlmeterTests.executable = sys.argv.pop(1)
        RtlmeterTests.rtlmeter_dir = sys.argv.pop(1)
    # Remaining positional args (those not starting with '-') are design names.
    designs = []
    while len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        designs.append(sys.argv.pop(1))
    add_design_tests(RtlmeterTests.rtlmeter_dir, designs)
    unittest.main()
