import unittest

import pyslang
import pyslang_netlist


class TestNetlistNode(unittest.TestCase):

    def test_import(self):
        self.assertTrue(hasattr(pyslang_netlist, "NetlistNode"))
        self.assertTrue(hasattr(pyslang_netlist, "Port"))
        self.assertTrue(hasattr(pyslang_netlist, "Variable"))
        self.assertTrue(hasattr(pyslang_netlist, "State"))
        self.assertTrue(hasattr(pyslang_netlist, "Assignment"))
        self.assertTrue(hasattr(pyslang_netlist, "Conditional"))
        self.assertTrue(hasattr(pyslang_netlist, "Case"))
        self.assertTrue(hasattr(pyslang_netlist, "Merge"))
        self.assertTrue(hasattr(pyslang_netlist, "NodeKind"))

    def test_get_location(self):
        tree = pyslang.syntax.SyntaxTree.fromText(
            "module m(output logic a); assign a = 1; endmodule"
        )
        compilation = pyslang.ast.Compilation()
        compilation.addSyntaxTree(tree)
        self.assertEqual(len(compilation.getAllDiagnostics()), 0)
        compilation.freeze()
        analysis_manager = pyslang.analysis.AnalysisManager()
        analysis_manager.analyze(compilation)
        graph = pyslang_netlist.NetlistGraph()
        graph.build(compilation, analysis_manager)

        port = graph.lookup("m.a")
        location = port.get_location()
        self.assertIsInstance(location, tuple)
        self.assertEqual(len(location), 3)
        self.assertGreater(location[1], 0)

        for node in graph:
            self.assertIsInstance(node.get_location(), tuple)


if __name__ == "__main__":
    unittest.main()
