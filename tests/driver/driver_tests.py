import subprocess
import sys
import unittest


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
        self.assertIn(
            """rca.sv:5:31: note: input port i_op0
   input  logic [p_width-1:0] i_op0,
                              ^
rca.sv:5:31: note: value rca.i_op0[0:0]
   input  logic [p_width-1:0] i_op0,
                              ^
rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
rca.sv:10:23: note: value rca.carry[1:1]
  logic [p_width-1:0] carry;
                      ^
rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
rca.sv:10:23: note: value rca.carry[2:2]
  logic [p_width-1:0] carry;
                      ^
rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
rca.sv:10:23: note: value rca.carry[3:3]
  logic [p_width-1:0] carry;
                      ^
rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
rca.sv:10:23: note: value rca.carry[4:4]
  logic [p_width-1:0] carry;
                      ^
rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
rca.sv:10:23: note: value rca.carry[5:5]
  logic [p_width-1:0] carry;
                      ^
rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
rca.sv:10:23: note: value rca.carry[6:6]
  logic [p_width-1:0] carry;
                      ^
rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
rca.sv:11:23: note: value rca.sum[7:0]
  logic [p_width-1:0] sum;
                      ^
rca.sv:27:7: note: assignment
      sum_q <= sum;
      ^
rca.sv:12:23: note: value rca.sum_q[7:0]
  logic [p_width-1:0] sum_q;
                      ^
rca.sv:12:23: note: value rca.sum_q[7:0]
  logic [p_width-1:0] sum_q;
                      ^
rca.sv:16:10: note: assignment
  assign {o_co, o_sum} = {co_q, sum_q};
         ^
rca.sv:7:31: note: value rca.o_sum[7:0]
   output logic [p_width-1:0] o_sum,
                              ^
rca.sv:7:31: note: output port o_sum
   output logic [p_width-1:0] o_sum,
                              ^
""",
            result.stdout,
        )

    def test_rca_symbols(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--report-symbols",
                "-",
            ],
            capture_output=True,
            text=True,
        )
        print(result.stdout)
        self.assertEqual(result.returncode, 0)
        self.assertIn(
            """Value i_clk rca.i_clk rca.sv:3:31
Value i_rst rca.i_rst rca.sv:4:31
Value i_op0 rca.i_op0 rca.sv:5:31
Value i_op1 rca.i_op1 rca.sv:6:31
Value o_sum rca.o_sum rca.sv:7:31
Value o_co rca.o_co rca.sv:8:31
Value carry rca.carry rca.sv:10:23
Value sum rca.sum rca.sv:11:23
Value sum_q rca.sum_q rca.sv:12:23
Value co_q rca.co_q rca.sv:13:23
""",
            result.stdout,
        )

    def test_rca_drivers(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--report-drivers",
                "-",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn(
            """rca.p_width                                                  rca.sv:2:15
rca.i_clk                                                    rca.sv:3:31
  [0:0] by cont prefix=i_clk                                 rca.sv:3:31
rca.i_rst                                                    rca.sv:4:31
  [0:0] by cont prefix=i_rst                                 rca.sv:4:31
rca.i_op0                                                    rca.sv:5:31
  [0:7] by cont prefix=i_op0                                 rca.sv:5:31
rca.i_op1                                                    rca.sv:6:31
  [0:7] by cont prefix=i_op1                                 rca.sv:6:31
rca.o_sum                                                    rca.sv:7:31
  [0:7] by cont prefix=o_sum                                 rca.sv:16:17
rca.o_co                                                     rca.sv:8:31
  [0:0] by cont prefix=o_co                                  rca.sv:16:11
rca.carry                                                    rca.sv:10:23
  [0:0] by cont prefix=carry[0]                              rca.sv:15:10
  [1:1] by cont prefix=carry[1]                              rca.sv:19:13
  [2:2] by cont prefix=carry[2]                              rca.sv:19:13
  [3:3] by cont prefix=carry[3]                              rca.sv:19:13
  [4:4] by cont prefix=carry[4]                              rca.sv:19:13
  [5:5] by cont prefix=carry[5]                              rca.sv:19:13
  [6:6] by cont prefix=carry[6]                              rca.sv:19:13
  [7:7] by cont prefix=carry[7]                              rca.sv:19:13
rca.sum                                                      rca.sv:11:23
  [0:0] by cont prefix=sum[0]                                rca.sv:19:25
  [1:1] by cont prefix=sum[1]                                rca.sv:19:25
  [2:2] by cont prefix=sum[2]                                rca.sv:19:25
  [3:3] by cont prefix=sum[3]                                rca.sv:19:25
  [4:4] by cont prefix=sum[4]                                rca.sv:19:25
  [5:5] by cont prefix=sum[5]                                rca.sv:19:25
  [6:6] by cont prefix=sum[6]                                rca.sv:19:25
rca.sum_q                                                    rca.sv:12:23
  [0:7] by proc prefix=sum_q                                 rca.sv:24:7
rca.co_q                                                     rca.sv:13:23
  [0:0] by proc prefix=co_q                                  rca.sv:25:7
rca.genblk1[0].i                                             rca.sv:18:15
rca.genblk1[1].i                                             rca.sv:18:15
rca.genblk1[2].i                                             rca.sv:18:15
rca.genblk1[3].i                                             rca.sv:18:15
rca.genblk1[4].i                                             rca.sv:18:15
rca.genblk1[5].i                                             rca.sv:18:15
rca.genblk1[6].i                                             rca.sv:18:15
""",
            result.stdout,
        )


if __name__ == "__main__":
    if len(sys.argv) > 1:
        DriverTests.executable = sys.argv.pop(1)
    unittest.main()
