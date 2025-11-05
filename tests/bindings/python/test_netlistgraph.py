import unittest

import pyslang
import pyslang_netlist


class NetlistGraphTest:
    """
    Helper class to build a netlist graph from given SystemVerilog code and hold
    on to the references to the syntax tree, compilation, analysis manager,
    graph, and builder. This prevents them being garbage collected while tests
    are running.
    """

    def __init__(self, code: str):

        # Compile the test.
        self.tree = pyslang.SyntaxTree.fromText(code)
        self.compilation = pyslang.Compilation()
        self.compilation.addSyntaxTree(self.tree)
        diagnostics = self.compilation.getAllDiagnostics()
        assert len(diagnostics) == 0
        self.compilation.freeze()

        # Run analysis.
        self.analysis_manager = pyslang.AnalysisManager()
        self.analysis_manager.analyze(self.compilation)

        # Build the netlist.
        self.graph = pyslang_netlist.NetlistGraph()
        builder = pyslang_netlist.NetlistBuilder(
            self.compilation, self.analysis_manager, self.graph
        )
        builder.run(self.compilation)
        builder.finalize()
        self.builder = builder


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
        test = NetlistGraphTest(code)
        self.assertEqual(test.graph.num_nodes(), 2)

    def test_lookup_existing(self):
        code = "module m(output logic a); assign a = 1; endmodule"
        test = NetlistGraphTest(code)
        node = test.graph.lookup("m.a")
        self.assertIsNotNone(node)
        self.assertEqual(node.symbol.name, "a")

    def test_find_path(self):
        code = "module m(input logic a, output logic b); assign b = a; endmodule"
        test = NetlistGraphTest(code)
        start = test.graph.lookup("m.a")
        end = test.graph.lookup("m.b")
        finder = pyslang_netlist.PathFinder(test.builder)
        path = finder.find(start, end)
        self.assertTrue(path.empty() is False)


if __name__ == "__main__":
    unittest.main()
