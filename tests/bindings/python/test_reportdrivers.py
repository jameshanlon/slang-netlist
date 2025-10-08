import unittest

import py_slang_netlist


class TestReportDrivers(unittest.TestCase):

    def test_import(self):
        """
        Test that the class can be imported and constructed.
        """
        self.assertTrue(hasattr(py_slang_netlist, "ReportDrivers"))

    def test_methods_exist(self):
        """
        Test that the class methods exits.
        """
        cls = py_slang_netlist.ReportDrivers
        self.assertTrue(hasattr(cls, "report"))


if __name__ == "__main__":
    unittest.main()
