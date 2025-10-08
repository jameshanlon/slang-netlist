import unittest

import pyslang
import pyslang_netlist


class TestReportDrivers(unittest.TestCase):
    def test_reportdrivers_import(self):
        self.assertTrue(hasattr(pyslang_netlist, "ReportDrivers"))

    def test_reportdrivers_methods(self):
        cls = pyslang_netlist.ReportDrivers
        self.assertTrue(hasattr(cls, "report"))

    def test_reportdrivers_construction(self):
        code = "module m(output logic a); assign a = 1; endmodule"
        tree = pyslang.SyntaxTree.fromText(code)
        compilation = pyslang.Compilation()
        compilation.addSyntaxTree(tree)
        diagnostics = compilation.getAllDiagnostics()
        assert len(diagnostics) == 0
        analysis_manager = pyslang.AnalysisManager()
        report_drivers = pyslang_netlist.ReportDrivers(compilation, analysis_manager)
        report_drivers.run(compilation)
        self.assertTrue("m.a" in report_drivers.report())


if __name__ == "__main__":
    unittest.main()
