import contextlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

from utilities import fuzzy_compare_strings

FANOUT_SV = """\
module m(input logic a, output logic x, output logic y);
  assign x = a;
  assign y = a;
endmodule
"""

FANIN_SV = """\
module m(input logic a, input logic b, output logic y);
  assign y = a + b;
endmodule
"""

SENS_SV = """\
module m(input logic clk, input logic rst_n,
         input logic d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 1'b0;
    else q <= d;
endmodule
"""

CONST_SV = """\
module m(input logic a, output logic [3:0] y,
         output logic w);
  assign y = 4'hA;
  assign w = a;
endmodule
"""


class DriverTests(unittest.TestCase):
    executable = ...

    def run_tool(self, *args, source=None, check=True):
        """Invoke slang-netlist with the given arguments.

        If source is given, write it to a temporary .sv file and pass its
        path as the first argument. Assert a zero exit status unless
        check is False. Return the CompletedProcess.
        """
        with contextlib.ExitStack() as stack:
            if source is not None:
                sv = tempfile.NamedTemporaryFile(suffix=".sv", mode="w", delete=False)
                stack.callback(os.unlink, sv.name)
                sv.write(source)
                sv.close()
                args = (sv.name, *args)
            result = subprocess.run(
                [self.executable, *args], capture_output=True, text=True
            )
        if check:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def assert_fails(self, *args):
        """Assert the tool exits non-zero for the given arguments."""
        self.assertNotEqual(self.run_tool(*args, check=False).returncode, 0)

    @staticmethod
    @contextlib.contextmanager
    def temp_path(suffix):
        """Yield a temporary file path, removed on exit."""
        f = tempfile.NamedTemporaryFile(suffix=suffix, delete=False)
        f.close()
        try:
            yield f.name
        finally:
            os.unlink(f.name)

    @staticmethod
    def _parse_stats(stdout):
        """Extract and parse the JSON stats line from stdout."""
        for line in stdout.splitlines():
            line = line.strip()
            if line.startswith("{") and "time_seconds" in line:
                return json.loads(line)
        return None

    def test_help(self):
        self.assertIn("USAGE:", self.run_tool("--help").stdout)

    def test_version(self):
        self.assertIn("slang-netlist version", self.run_tool("--version").stdout)

    def test_rca_path(self):
        r = self.run_tool(
            "rca.sv", "--from", "rca.i_op0", "--to", "rca.o_sum", "--no-colours"
        )
        # The exact path found depends on node ordering, which is
        # non-deterministic with parallel DFA execution. Verify that a valid
        # path was found by checking the start, end, and key intermediate
        # elements.
        self.assertIn("note: input port i_op0", r.stdout)
        self.assertIn("note: output port o_sum", r.stdout)
        self.assertIn("note: assignment", r.stdout)
        self.assertIn("rca.sum_q", r.stdout)
        self.assertIn("rca.o_sum", r.stdout)

    def test_rca_registers(self):
        r = self.run_tool("rca.sv", "--report-registers")
        self.assertTrue(
            fuzzy_compare_strings(
                """
Name       Location
rca.sum_q  rca.sv:12:23
rca.co_q   rca.sv:13:23
""",
                r.stdout,
            )
        )

    def test_comb_loop(self):
        r = self.run_tool("comb-loop.sv", "--comb-loops", "--no-colours")
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
                r.stdout,
            )
        )

    def test_no_comb_loop(self):
        r = self.run_tool("rca.sv", "--comb-loops", "--no-colours")
        self.assertIn("No combinational loops detected in the design.", r.stdout)

    def test_save_netlist(self):
        with self.temp_path(".json") as netlist:
            self.run_tool("rca.sv", "--save-netlist", netlist)
            with open(netlist) as f:
                data = json.load(f)
        self.assertEqual(data["version"], 3)
        self.assertIn("fileTable", data)
        self.assertIn("nodes", data)
        self.assertIn("edges", data)
        self.assertGreater(len(data["nodes"]), 0)
        self.assertGreater(len(data["edges"]), 0)

    def test_load_netlist_path(self):
        with self.temp_path(".json") as netlist:
            self.run_tool("rca.sv", "--save-netlist", netlist)
            r = self.run_tool(
                "--load-netlist", netlist, "--from", "rca.i_op0", "--to", "rca.o_sum"
            )
        self.assertIn("input port i_op0", r.stdout)
        self.assertIn("output port o_sum", r.stdout)

    def test_load_netlist_comb_loops(self):
        with self.temp_path(".json") as netlist:
            self.run_tool("comb-loop.sv", "--save-netlist", netlist)
            r = self.run_tool("--load-netlist", netlist, "--comb-loops")
        self.assertIn("Combinational loop detected:", r.stdout)

    def test_load_netlist_registers(self):
        with self.temp_path(".json") as netlist:
            self.run_tool("rca.sv", "--save-netlist", netlist)
            r = self.run_tool("--load-netlist", netlist, "--report-registers")
        self.assertIn("rca.sum_q", r.stdout)
        self.assertIn("rca.co_q", r.stdout)

    def test_stats_json_full_build(self):
        r = self.run_tool("rca.sv", "--report-registers", "--stats-json")
        stats = self._parse_stats(r.stdout)
        self.assertIsNotNone(stats)
        times = stats["time_seconds"]
        for phase in ("parsing", "elaboration", "analysis", "netlist"):
            self.assertIsInstance(times[phase], float)
            self.assertGreater(times[phase], 0)
        self.assertIsInstance(stats["peak_rss_bytes"], int)
        self.assertGreater(stats["peak_rss_bytes"], 0)
        # Register output should still be present.
        self.assertIn("rca.sum_q", r.stdout)

    def test_stats_json_not_present_without_flag(self):
        """Stats JSON is not emitted when --stats-json is not specified."""
        r = self.run_tool("rca.sv", "--report-registers")
        self.assertIsNone(self._parse_stats(r.stdout))

    def test_find_wildcard(self):
        r = self.run_tool("rca.sv", "--find", "rca.i_*")
        for name in ("rca.i_clk", "rca.i_rst", "rca.i_op0", "rca.i_op1"):
            self.assertIn(name, r.stdout)
        self.assertNotIn("rca.o_sum", r.stdout)

    def test_find_wildcard_no_match(self):
        # A pattern with no matches is not an error.
        self.run_tool("rca.sv", "--find", "nonexistent.*")

    def test_find_regex(self):
        r = self.run_tool("rca.sv", "--find-regex", r"rca\.o_.*")
        self.assertIn("rca.o_sum", r.stdout)
        self.assertIn("rca.o_co", r.stdout)
        self.assertNotIn("rca.i_clk", r.stdout)

    def test_fan_out(self):
        r = self.run_tool("--fan-out", "m.a", source=FANOUT_SV)
        self.assertIn("m.x", r.stdout)
        self.assertIn("m.y", r.stdout)

    def test_fan_in(self):
        r = self.run_tool("--fan-in", "m.y", source=FANIN_SV)
        self.assertIn("m.a", r.stdout)
        self.assertIn("m.b", r.stdout)

    def test_fan_out_nonexistent(self):
        self.assert_fails("rca.sv", "--fan-out", "rca.nonexistent")

    def test_fan_in_nonexistent(self):
        self.assert_fails("rca.sv", "--fan-in", "rca.nonexistent")

    def test_sensitivity(self):
        r = self.run_tool("--sensitivity", "m.q", source=SENS_SV)
        self.assertIn("m.clk", r.stdout)
        self.assertIn("PosEdge", r.stdout)
        self.assertIn("m.rst_n", r.stdout)
        self.assertIn("NegEdge", r.stdout)

    def test_sensitivity_nonexistent(self):
        self.assert_fails("rca.sv", "--sensitivity", "rca.nonexistent")

    def test_constant_drivers(self):
        # A tied-off output reports its constant value.
        r = self.run_tool("--constant-drivers", "m.y", source=CONST_SV)
        self.assertIn("4'b1010", r.stdout)
        # An output driven by an input is not constant-driven.
        r = self.run_tool("--constant-drivers", "m.w", source=CONST_SV)
        self.assertNotIn("'b", r.stdout)

    def test_constant_drivers_nonexistent(self):
        self.assert_fails("rca.sv", "--constant-drivers", "rca.nonexistent")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        DriverTests.executable = sys.argv.pop(1)
    unittest.main()
