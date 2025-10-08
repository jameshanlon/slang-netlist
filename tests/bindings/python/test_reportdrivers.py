import unittest

import py_slang_netlist


class TestReportDrivers(unittest.TestCase):
    def test_reportdrivers_import(self):
        # Just test that the class can be imported and constructed
        self.assertTrue(hasattr(py_slang_netlist, "ReportDrivers"))

    def test_reportdrivers_methods(self):
        # We cannot construct ReportDrivers without C++ objects, but we can
        # check method presence
        cls = py_slang_netlist.ReportDrivers
        self.assertTrue(hasattr(cls, "report"))


if __name__ == "__main__":
    unittest.main()
