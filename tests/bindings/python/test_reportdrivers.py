import unittest

import pyslang
import pyslang_netlist


class TestReportDrivers(unittest.TestCase):
    def test_reportdrivers_import(self):
        self.assertTrue(hasattr(pyslang_netlist, "ReportDrivers"))

    def test_reportdrivers_methods(self):
        cls = pyslang_netlist.ReportDrivers
        self.assertTrue(hasattr(cls, "report"))

    def test_reportdrivers(self):
        code = "module m(output logic a); assign a = 1; endmodule"

        # Compile the test.
        tree = pyslang.SyntaxTree.fromText(code)
        compilation = pyslang.Compilation()
        compilation.addSyntaxTree(tree)
        diagnostics = compilation.getAllDiagnostics()
        assert len(diagnostics) == 0
        compilation.freeze()

        # Run analysis.
        analysis_manager = pyslang.AnalysisManager()
        analysis_manager.analyze(compilation)

        # Report drivers.
        report_drivers = pyslang_netlist.ReportDrivers(compilation, analysis_manager)
        report_drivers.run(compilation)
        self.assertEqual(
            report_drivers.report(),
            """m.a                                                          source:1:23
  [0:0] by cont prefix=a                                     source:1:34
""",
        )


if __name__ == "__main__":
    unittest.main()
