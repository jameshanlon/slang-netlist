import json
import os
import subprocess
import sys
import tempfile
import unittest

from utilities import fuzzy_compare_strings


class ReportTests(unittest.TestCase):
    executable = ...

    def test_help(self):
        result = subprocess.run(
            [self.executable, "--help"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("USAGE:", result.stdout)

    def test_version(self):
        result = subprocess.run(
            [self.executable, "--version"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("slang-report version", result.stdout)

    def test_ast_json(self):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outfile = f.name
        try:
            result = subprocess.run(
                [self.executable, "rca.sv", "--ast-json", outfile],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            with open(outfile) as f:
                data = json.load(f)
            self.assertEqual(data["name"], "$root")
            self.assertEqual(data["kind"], "Root")
            names = [m.get("name") for m in data.get("members", [])]
            self.assertIn("rca", names)
        finally:
            os.unlink(outfile)

    def test_no_action(self):
        result = subprocess.run(
            [self.executable, "rca.sv"],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no action specified", result.stderr)

    def test_ports(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--ports"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertTrue(
            fuzzy_compare_strings(
                """
Direction  Name       Location
In         rca.i_clk  rca.sv:3:31
In         rca.i_rst  rca.sv:4:31
In         rca.i_op0  rca.sv:5:31
In         rca.i_op1  rca.sv:6:31
Out        rca.o_sum  rca.sv:7:31
Out        rca.o_co   rca.sv:8:31
""",
                result.stdout,
            )
        )

    def test_variables(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--variables"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertTrue(
            fuzzy_compare_strings(
                """
Name       Location
rca.i_clk  rca.sv:3:31
rca.i_rst  rca.sv:4:31
rca.i_op0  rca.sv:5:31
rca.i_op1  rca.sv:6:31
rca.o_sum  rca.sv:7:31
rca.o_co   rca.sv:8:31
rca.carry  rca.sv:10:23
rca.sum    rca.sv:11:23
rca.sum_q  rca.sv:12:23
rca.co_q   rca.sv:13:23
""",
                result.stdout,
            )
        )

    def test_drivers(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--drivers"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertTrue(
            fuzzy_compare_strings(
                """
Value             Range  Driver    Type  Location
rca.p_width                              rca.sv:2:15
rca.i_clk                                rca.sv:3:31
↳                 [0]    i_clk     cont  rca.sv:3:31
rca.i_rst                                rca.sv:4:31
↳                 [0]    i_rst     cont  rca.sv:4:31
rca.i_op0                                rca.sv:5:31
↳                 [7:0]  i_op0     cont  rca.sv:5:31
rca.i_op1                                rca.sv:6:31
↳                 [7:0]  i_op1     cont  rca.sv:6:31
rca.o_sum                                rca.sv:7:31
↳                 [7:0]  o_sum     cont  rca.sv:16:17
rca.o_co                                 rca.sv:8:31
↳                 [0]    o_co      cont  rca.sv:16:11
rca.carry                                rca.sv:10:23
↳                 [0]    carry[0]  cont  rca.sv:15:10
↳                 [1]    carry[1]  cont  rca.sv:19:13
↳                 [2]    carry[2]  cont  rca.sv:19:13
↳                 [3]    carry[3]  cont  rca.sv:19:13
↳                 [4]    carry[4]  cont  rca.sv:19:13
↳                 [5]    carry[5]  cont  rca.sv:19:13
↳                 [6]    carry[6]  cont  rca.sv:19:13
↳                 [7]    carry[7]  cont  rca.sv:19:13
rca.sum                                  rca.sv:11:23
↳                 [0]    sum[0]    cont  rca.sv:19:25
↳                 [1]    sum[1]    cont  rca.sv:19:25
↳                 [2]    sum[2]    cont  rca.sv:19:25
↳                 [3]    sum[3]    cont  rca.sv:19:25
↳                 [4]    sum[4]    cont  rca.sv:19:25
↳                 [5]    sum[5]    cont  rca.sv:19:25
↳                 [6]    sum[6]    cont  rca.sv:19:25
rca.sum_q                                rca.sv:12:23
↳                 [7:0]  sum_q     proc  rca.sv:24:7
rca.co_q                                 rca.sv:13:23
↳                 [0]    co_q      proc  rca.sv:25:7
rca.genblk1[0].i                         rca.sv:18:15
rca.genblk1[1].i                         rca.sv:18:15
rca.genblk1[2].i                         rca.sv:18:15
rca.genblk1[3].i                         rca.sv:18:15
rca.genblk1[4].i                         rca.sv:18:15
rca.genblk1[5].i                         rca.sv:18:15
rca.genblk1[6].i                         rca.sv:18:15
""",
                result.stdout,
            )
        )


if __name__ == "__main__":
    if len(sys.argv) > 1:
        ReportTests.executable = sys.argv.pop(1)
    unittest.main()
