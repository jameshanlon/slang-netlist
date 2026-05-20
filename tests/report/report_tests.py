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

    def test_ast_json_scope_literal(self):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outfile = f.name
        try:
            result = subprocess.run(
                [
                    self.executable,
                    "rca.sv",
                    "--ast-json",
                    outfile,
                    "--scope",
                    "rca.sum_q",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            with open(outfile) as f:
                data = json.load(f)
            self.assertEqual(data["name"], "sum_q")
            self.assertEqual(data["kind"], "Variable")
        finally:
            os.unlink(outfile)

    def test_ast_json_scope_not_found(self):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outfile = f.name
        try:
            result = subprocess.run(
                [
                    self.executable,
                    "rca.sv",
                    "--ast-json",
                    outfile,
                    "--scope",
                    "rca.bogus",
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("scope 'rca.bogus' not found", result.stderr)
        finally:
            os.unlink(outfile)

    def test_ast_json_scope_removed_flag(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--ast-json",
                "-",
                "--ast-json-scope",
                "rca",
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--ast-json-scope", result.stderr)

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

    def test_ports_json(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--ports", "--format", "json"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        data = json.loads(result.stdout)
        self.assertEqual(len(data), 6)
        first = data[0]
        self.assertEqual(first["name"], "rca.i_clk")
        self.assertEqual(first["direction"], "In")
        self.assertEqual(first["location"], "rca.sv:3:31")
        self.assertEqual(data[-1]["name"], "rca.o_co")
        self.assertEqual(data[-1]["direction"], "Out")

    def test_variables_json(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--variables", "--format", "json"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        data = json.loads(result.stdout)
        names = [v["name"] for v in data]
        self.assertIn("rca.carry", names)
        self.assertIn("rca.sum_q", names)
        for v in data:
            self.assertIn("location", v)

    def test_drivers_json(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--drivers", "--format", "json"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        data = json.loads(result.stdout)
        by_value = {v["value"]: v for v in data}

        carry = by_value["rca.carry"]
        self.assertEqual(carry["location"], "rca.sv:10:23")
        self.assertEqual(len(carry["drivers"]), 8)
        self.assertEqual(carry["drivers"][0]["range"], "[0]")
        self.assertEqual(carry["drivers"][0]["driver"], "carry[0]")
        self.assertEqual(carry["drivers"][0]["kind"], "cont")

        sum_q = by_value["rca.sum_q"]
        self.assertEqual(len(sum_q["drivers"]), 1)
        self.assertEqual(sum_q["drivers"][0]["kind"], "proc")
        self.assertEqual(sum_q["drivers"][0]["range"], "[7:0]")

        self.assertEqual(by_value["rca.p_width"]["drivers"], [])

    def test_unknown_format(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--ports", "--format", "bogus"],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown --format value", result.stderr)

    def test_scope_variables(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--variables",
                "--scope",
                "rca.sum_q",
                "--scope",
                "rca.co_q",
                "--format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        data = json.loads(result.stdout)
        names = [v["name"] for v in data]
        self.assertEqual(names, ["rca.sum_q", "rca.co_q"])

    def test_scope_drivers(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--drivers",
                "--scope",
                "rca.sum_q",
                "--format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        data = json.loads(result.stdout)
        self.assertEqual(len(data), 1)
        self.assertEqual(data[0]["value"], "rca.sum_q")
        self.assertEqual(len(data[0]["drivers"]), 1)
        self.assertEqual(data[0]["drivers"][0]["kind"], "proc")

    def test_scope_ports_subscope(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--ports",
                "--scope",
                "rca",
                "--format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        data = json.loads(result.stdout)
        self.assertEqual(len(data), 6)

    def test_scope_not_found(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--ports", "--scope", "rca.bogus"],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("scope 'rca.bogus' not found", result.stderr)

    def test_scope_glob_single_segment(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--variables",
                "--scope",
                "rca.sum_*",
                "--format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        names = [v["name"] for v in json.loads(result.stdout)]
        self.assertEqual(names, ["rca.sum_q"])

    def test_scope_glob_recursive(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--drivers",
                "--scope",
                "rca.**.i",
                "--format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        values = [v["value"] for v in json.loads(result.stdout)]
        self.assertEqual(len(values), 7)
        for v in values:
            self.assertTrue(v.startswith("rca.genblk1["))
            self.assertTrue(v.endswith("].i"))

    def test_scope_glob_question_mark(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--drivers",
                "--scope",
                "rca.genblk1[?].i",
                "--format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        values = [v["value"] for v in json.loads(result.stdout)]
        self.assertEqual(len(values), 7)

    def test_scope_glob_dedupe(self):
        result = subprocess.run(
            [
                self.executable,
                "rca.sv",
                "--variables",
                "--scope",
                "rca.sum_*",
                "--scope",
                "rca.*_q",
                "--format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        names = [v["name"] for v in json.loads(result.stdout)]
        self.assertEqual(names, ["rca.sum_q", "rca.co_q"])

    def test_scope_glob_no_match(self):
        result = subprocess.run(
            [self.executable, "rca.sv", "--ports", "--scope", "rca.bogus_*"],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("scope 'rca.bogus_*' matched no symbols", result.stderr)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        ReportTests.executable = sys.argv.pop(1)
    unittest.main()
