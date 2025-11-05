import unittest

import pyslang
import pyslang_netlist


class TestNetlistGraph(unittest.TestCase):

    def test_import(self):
        self.assertTrue(hasattr(pyslang_netlist, "NetlistGraph"))

    def test_constructor(self):
        graph = pyslang_netlist.NetlistGraph()
        self.assertIsInstance(graph, pyslang_netlist.NetlistGraph)

    def test_lookup_nonexistent(self):
        graph = pyslang_netlist.NetlistGraph()
        # Should return None for any name in an empty graph.
        self.assertIsNone(graph.lookup("nonexistent"))

    def test_build_graph(self):
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

        # Build the netlist.
        graph = pyslang_netlist.NetlistGraph()
        builder = pyslang_netlist.NetlistBuilder(compilation, analysis_manager, graph)
        builder.run(compilation)
        builder.finalize()

        # Query the graph.
        node = graph.lookup("m.a")
        self.assertIsNotNone(node)
        self.assertEqual(node.kind, pyslang_netlist.NodeKind.Port)


if __name__ == "__main__":
    unittest.main()
