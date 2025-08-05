import subprocess
import sys
import unittest


class TestDriver(unittest.TestCase):

    def setUp(self):
        # First arg after the script name is the executable.
        self.executable = sys.argv[1]
        # Remove the executable from the argument list.
        sys.argv.pop(1)

    def test_help(self):
        result = subprocess.run(
            [self.executable, "--help"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("USAGE:", result.stdout, "Help output is missing 'USAGE:'")

    def test_version(self):
        result = subprocess.run(
            [self.executable, "--version"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "slang-netlist version",
            result.stdout,
            "Version output is missing 'slang-netlist version'",
        )

    def test_rca_path(self):
        result = subprocess.run(
            [
                self.executable,
                "tests/driver/ripple_carry_adder.sv",
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
        self.assertIn(
            """
tests/driver/rca.sv:6:31: note: input port i_op1
   input  logic [p_width-1:0] i_op1,
                              ^
tests/driver/rca.sv:6:31: note: value rca.i_op1[0:0]
   input  logic [p_width-1:0] i_op1,
                              ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[1:1]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[2:2]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[3:3]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[4:4]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[5:5]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[6:6]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[7:7]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:28:7: note: assignment
      co_q  <= carry[p_width-1];
      ^
tests/driver/rca.sv:13:23: note: value rca.co_q[0:0]
  logic               co_q;
                      ^
tests/driver/rca.sv:13:23: note: value rca.co_q[0:0]
  logic               co_q;
                      ^
tests/driver/rca.sv:16:10: note: assignment
  assign {o_co, o_sum} = {co_q, sum_q};
         ^
tests/driver/rca.sv:7:31: note: value rca.o_sum[7:0]
   output logic [p_width-1:0] o_sum,
                              ^
tests/driver/rca.sv:7:31: note: output port o_sum
   output logic [p_width-1:0] o_sum,
                              ^
"""
        )

    def test_rca_symbols(self):
        result = subprocess.run(
            [
                self.executable,
                "tests/driver/ripple_carry_adder.sv",
                "--report-symbols",
                "-",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn(
            """
Value p_width rca.p_width tests/driver/rca.sv:2:15
Port i_clk rca.i_clk tests/driver/rca.sv:3:31
Value i_clk rca.i_clk tests/driver/rca.sv:3:31
Port i_rst rca.i_rst tests/driver/rca.sv:4:31
Value i_rst rca.i_rst tests/driver/rca.sv:4:31
Port i_op0 rca.i_op0 tests/driver/rca.sv:5:31
Value i_op0 rca.i_op0 tests/driver/rca.sv:5:31
Port i_op1 rca.i_op1 tests/driver/rca.sv:6:31
Value i_op1 rca.i_op1 tests/driver/rca.sv:6:31
Port o_sum rca.o_sum tests/driver/rca.sv:7:31
Value o_sum rca.o_sum tests/driver/rca.sv:7:31
Port o_co rca.o_co tests/driver/rca.sv:8:31
Value o_co rca.o_co tests/driver/rca.sv:8:31
Value carry rca.carry tests/driver/rca.sv:10:23
Value sum rca.sum tests/driver/rca.sv:11:23
Value sum_q rca.sum_q tests/driver/rca.sv:12:23
Value co_q rca.co_q tests/driver/rca.sv:13:23
Value i rca.genblk1[0].i tests/driver/rca.sv:18:15
Value i rca.genblk1[1].i tests/driver/rca.sv:18:15
Value i rca.genblk1[2].i tests/driver/rca.sv:18:15
Value i rca.genblk1[3].i tests/driver/rca.sv:18:15
Value i rca.genblk1[4].i tests/driver/rca.sv:18:15
Value i rca.genblk1[5].i tests/driver/rca.sv:18:15
Value i rca.genblk1[6].i tests/driver/rca.sv:18:15
"""
        )
