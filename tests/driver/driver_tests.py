import subprocess
import unittest
import sys


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
        self.assertEqual(result.returncode, 1, "Help command failed")
        self.assertIn("USAGE:", result.stdout, "Help output is missing 'USAGE:'")

    def test_version(self):
        result = subprocess.run(
            [self.executable, "--version"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 1, "Version command failed")
        self.assertIn(
            "slang-netlist version",
            result.stdout,
            "Version output is missing 'slang-netlist version'",
        )
