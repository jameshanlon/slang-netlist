import unittest

import py_slang_netlist
import pyslang


class TestReportDrivers(unittest.TestCase):
    def test_reportdrivers_import(self):
        self.assertTrue(hasattr(py_slang_netlist, "ReportDrivers"))

    def test_reportdrivers_methods(self):
        cls = py_slang_netlist.ReportDrivers
        self.assertTrue(hasattr(cls, "report"))

    def test_reportdrivers_construction(self):
        code = "module m(); endmodule"
        tree = pyslang.SyntaxTree.fromText(code)
        compilation = pyslang.Compilation()
        compilation.addSyntaxTree(tree)
        diagnostics = compilation.getAllDiagnostics()
        assert len(diagnostics) == 0
        analysis_manager = pyslang.AnalysisManager()
        report_drivers = py_slang_netlist.ReportDrivers(compilation, analysis_manager)
        compilation.getRoot().visit(report_drivers)


if __name__ == "__main__":
    unittest.main()
