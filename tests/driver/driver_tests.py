import json
import os
import subprocess
import sys
import tempfile
import unittest

from utilities import fuzzy_compare_strings


class DriverTests(unittest.TestCase):
    executable = ...

    def test_help(self):
        result = subprocess.run(
            [self.executable, "--help"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn(
            "USAGE:",
            result.stdout,
        )

    def test_version(self):
        result = subprocess.run(
            [self.executable, "--version"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn(
            "slang-netlist version",
            result.stdout,
        )

    def test_rca_path(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--from",
                "rca.i_op0",
                "--to",
                "rca.o_sum",
                "--no-colours",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        # The exact path found depends on node ordering, which is
        # non-deterministic with parallel DFA execution. Verify that a valid
        # path was found by checking the start, end, and key intermediate
        # elements.
        self.assertIn("note: input port i_op0", result.stdout)
        self.assertIn("note: output port o_sum", result.stdout)
        self.assertIn("note: assignment", result.stdout)
        self.assertIn("rca.sum_q", result.stdout)
        self.assertIn("rca.o_sum", result.stdout)

    def test_rca_registers(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--report-registers",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertTrue(
            fuzzy_compare_strings(
                """
Name       Location
rca.sum_q  rca.sv:12:23
rca.co_q   rca.sv:13:23
""",
                result.stdout,
            )
        )

    def test_comb_loop(self):
        result = subprocess.run(
            [
                self.executable,
                "comb-loop.sv",
                "--comb-loops",
                "--no-colours",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertTrue(
            fuzzy_compare_strings(
                """
Combinational loop detected:

comb-loop.sv:3:16: note: input port x
module t(input x, output y);
               ^
comb-loop.sv:3:16: note: value m.t.x[0]
module t(input x, output y);
               ^
comb-loop.sv:4:10: note: assignment
  assign y = x;
         ^
comb-loop.sv:3:26: note: value m.t.y[0]
module t(input x, output y);
                         ^
comb-loop.sv:3:26: note: output port y
module t(input x, output y);
                         ^
comb-loop.sv:8:11: note: value m.b[0]
  wire a, b;
          ^
comb-loop.sv:10:10: note: assignment
  assign a = b;
         ^
""",
                result.stdout,
            )
        )

    def test_no_comb_loop(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--comb-loops",
                "--no-colours",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("No combinational loops detected in the design.", result.stdout)

    def test_save_netlist(self):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outfile = f.name
        try:
            result = subprocess.run(
                [
                    self.executable,
                    "rca.sv",
                    "--save-netlist",
                    outfile,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            with open(outfile) as f:
                data = json.load(f)
            self.assertEqual(data["version"], 3)
            self.assertIn("fileTable", data)
            self.assertIn("nodes", data)
            self.assertIn("edges", data)
            self.assertGreater(len(data["nodes"]), 0)
            self.assertGreater(len(data["edges"]), 0)
        finally:
            os.unlink(outfile)

    def test_load_netlist_path(self):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outfile = f.name
        try:
            # Save the netlist first.
            result = subprocess.run(
                [
                    self.executable,
                    "rca.sv",
                    "--save-netlist",
                    outfile,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            # Load and find a path.
            result = subprocess.run(
                [
                    self.executable,
                    "--load-netlist",
                    outfile,
                    "--from",
                    "rca.i_op0",
                    "--to",
                    "rca.o_sum",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            self.assertIn("input port i_op0", result.stdout)
            self.assertIn("output port o_sum", result.stdout)
        finally:
            os.unlink(outfile)

    def test_load_netlist_comb_loops(self):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outfile = f.name
        try:
            # Save.
            result = subprocess.run(
                [
                    self.executable,
                    "comb-loop.sv",
                    "--save-netlist",
                    outfile,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            # Load and detect comb loops.
            result = subprocess.run(
                [
                    self.executable,
                    "--load-netlist",
                    outfile,
                    "--comb-loops",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            self.assertIn("Combinational loop detected:", result.stdout)
        finally:
            os.unlink(outfile)

    def _parse_stats(self, stdout):
        """Extract and parse the JSON stats line from stdout."""
        for line in stdout.splitlines():
            line = line.strip()
            if line.startswith("{") and "time_seconds" in line:
                return json.loads(line)
        return None

    def test_stats_json_full_build(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--report-registers",
                "--stats-json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        stats = self._parse_stats(result.stdout)
        self.assertIsNotNone(stats)
        times = stats["time_seconds"]
        self.assertIn("parsing", times)
        self.assertIn("elaboration", times)
        self.assertIn("analysis", times)
        self.assertIn("netlist", times)
        for phase in ("parsing", "elaboration", "analysis", "netlist"):
            self.assertIsInstance(times[phase], float)
            self.assertGreater(times[phase], 0)
        self.assertIn("peak_rss_bytes", stats)
        self.assertIsInstance(stats["peak_rss_bytes"], int)
        self.assertGreater(stats["peak_rss_bytes"], 0)
        # Register output should still be present.
        self.assertIn("rca.sum_q", result.stdout)

    def test_stats_json_not_present_without_flag(self):
        """Stats JSON is not emitted when --stats-json is not specified."""
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--report-registers",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIsNone(self._parse_stats(result.stdout))

    def test_load_netlist_registers(self):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outfile = f.name
        try:
            # Save.
            result = subprocess.run(
                [
                    self.executable,
                    "rca.sv",
                    "--save-netlist",
                    outfile,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            # Load and report registers.
            result = subprocess.run(
                [
                    self.executable,
                    "--load-netlist",
                    outfile,
                    "--report-registers",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            self.assertIn("rca.sum_q", result.stdout)
            self.assertIn("rca.co_q", result.stdout)
        finally:
            os.unlink(outfile)

    def test_find_wildcard(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--find",
                "rca.i_*",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("rca.i_clk", result.stdout)
        self.assertIn("rca.i_rst", result.stdout)
        self.assertIn("rca.i_op0", result.stdout)
        self.assertIn("rca.i_op1", result.stdout)
        self.assertNotIn("rca.o_sum", result.stdout)

    def test_find_wildcard_no_match(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--find",
                "nonexistent.*",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)

    def test_find_regex(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--find-regex",
                r"rca\.o_.*",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("rca.o_sum", result.stdout)
        self.assertIn("rca.o_co", result.stdout)
        self.assertNotIn("rca.i_clk", result.stdout)

    def test_fan_out(self):
        with tempfile.NamedTemporaryFile(suffix=".sv", delete=False, mode="w") as f:
            f.write(
                "module m(input logic a, output logic x, output logic y);\n"
                "  assign x = a;\n"
                "  assign y = a;\n"
                "endmodule\n"
            )
            svfile = f.name
        try:
            result = subprocess.run(
                [self.executable, svfile, "--fan-out", "m.a"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            self.assertIn("m.x", result.stdout)
            self.assertIn("m.y", result.stdout)
        finally:
            os.unlink(svfile)

    def test_fan_in(self):
        with tempfile.NamedTemporaryFile(suffix=".sv", delete=False, mode="w") as f:
            f.write(
                "module m(input logic a, input logic b, output logic y);\n"
                "  assign y = a + b;\n"
                "endmodule\n"
            )
            svfile = f.name
        try:
            result = subprocess.run(
                [self.executable, svfile, "--fan-in", "m.y"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            self.assertIn("m.a", result.stdout)
            self.assertIn("m.b", result.stdout)
        finally:
            os.unlink(svfile)

    def test_fan_out_nonexistent(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--fan-out",
                "rca.nonexistent",
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)

    def test_fan_in_nonexistent(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--fan-in",
                "rca.nonexistent",
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        DriverTests.executable = sys.argv.pop(1)
    unittest.main()
