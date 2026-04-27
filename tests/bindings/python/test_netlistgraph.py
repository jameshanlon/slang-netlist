import unittest

import pyslang
import pyslang_netlist


class NetlistGraphTest:
    """
    Helper class to build a netlist graph from given SystemVerilog code and hold
    on to the references to the syntax tree, compilation, analysis manager,
    and graph. This prevents them being garbage collected while tests are
    running.
    """

    def __init__(self, code: str):

        # Compile the test.
        self.tree = pyslang.syntax.SyntaxTree.fromText(code)
        self.compilation = pyslang.ast.Compilation()
        self.compilation.addSyntaxTree(self.tree)
        diagnostics = self.compilation.getAllDiagnostics()
        assert len(diagnostics) == 0
        self.compilation.freeze()

        # Run analysis.
        self.analysis_manager = pyslang.analysis.AnalysisManager()
        self.analysis_manager.analyze(self.compilation)

        # Build the netlist.
        self.graph = pyslang_netlist.NetlistGraph()
        self.graph.build(self.compilation, self.analysis_manager)


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
        self.assertEqual(test.graph.num_nodes(), 3)

    def test_resolve_assign_bits(self):
        tree = pyslang.syntax.SyntaxTree.fromText(
            "module m(input [1:0] c, output a, b);" "  assign {a,b} = c;" "endmodule"
        )
        compilation = pyslang.ast.Compilation()
        compilation.addSyntaxTree(tree)
        diagnostics = compilation.getAllDiagnostics()
        self.assertEqual(len(diagnostics), 0)
        compilation.freeze()
        am = pyslang.analysis.AnalysisManager()
        am.analyze(compilation)
        graph = pyslang_netlist.NetlistGraph()
        graph.build(compilation, am, resolve_assign_bits=True)
        self.assertGreater(graph.num_nodes(), 0)

    def test_lookup_existing(self):
        code = "module m(output logic a); assign a = 1; endmodule"
        test = NetlistGraphTest(code)
        node = test.graph.lookup("m.a")
        self.assertIsNotNone(node)
        self.assertEqual(node.name, "a")

    def test_find_path(self):
        code = "module m(input logic a, output logic b); assign b = a; endmodule"
        test = NetlistGraphTest(code)
        start = test.graph.lookup("m.a")
        end = test.graph.lookup("m.b")
        finder = pyslang_netlist.PathFinder()
        path = finder.find(start, end)
        self.assertTrue(path.empty() is False)

    def test_find_comb_path(self):
        code = """
        module m(input clk, input logic a, output logic b, output logic c);
            always_ff @(posedge clk)
                b <= a;
            assign c = a;
        endmodule
        """
        test = NetlistGraphTest(code)
        start = test.graph.lookup("m.a")
        seq_end = test.graph.lookup("m.b")
        comb_end = test.graph.lookup("m.c")
        finder = pyslang_netlist.PathFinder()
        # A path exists from a to b (through sequential state).
        self.assertFalse(finder.find(start, seq_end).empty())
        # But no combinatorial path exists from a to b.
        self.assertTrue(finder.find_comb(start, seq_end).empty())
        # A combinatorial path exists from a to c.
        self.assertFalse(finder.find_comb(start, comb_end).empty())

    def test_lookup_by_range(self):
        code = """
        module m(input logic [7:0] a, output logic [7:0] b);
            assign b = a;
        endmodule
        """
        test = NetlistGraphTest(code)
        # Port 'a' has bounds [0,7]. Query [3,5] overlaps.
        results = test.graph.lookup_by_range("m.a", 3, 5)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].kind, pyslang_netlist.NodeKind.Port)
        # Non-overlapping range returns empty.
        results = test.graph.lookup_by_range("m.a", 8, 15)
        self.assertEqual(len(results), 0)
        # Non-existent name returns empty.
        results = test.graph.lookup_by_range("m.nonexistent", 0, 0)
        self.assertEqual(len(results), 0)

    def test_iter_nodes(self):
        code = "module m(output logic a); assign a = 1; endmodule"
        test = NetlistGraphTest(code)
        graph = test.graph
        nodes = list(graph)
        self.assertEqual(len(nodes), graph.num_nodes())
        for node in nodes:
            self.assertIsInstance(node, pyslang_netlist.NetlistNode)

    def test_get_drivers(self):
        code = """
        module m(input logic a, output logic b);
            assign b = a;
        endmodule
        """
        test = NetlistGraphTest(code)
        drivers = test.graph.get_drivers("m.b", 0, 0)
        self.assertGreater(len(drivers), 0)

    def test_get_comb_fan_out(self):
        code = """
        module m(input logic a, output logic x, output logic y);
            assign x = a;
            assign y = a;
        endmodule
        """
        test = NetlistGraphTest(code)
        start = test.graph.lookup("m.a")
        fan_out = test.graph.get_comb_fan_out(start)
        names = {n.path for n in fan_out if hasattr(n, "path")}
        self.assertIn("m.x", names)
        self.assertIn("m.y", names)

    def test_get_comb_fan_in(self):
        code = """
        module m(input logic a, input logic b, output logic y);
            assign y = a + b;
        endmodule
        """
        test = NetlistGraphTest(code)
        end = test.graph.lookup("m.y")
        fan_in = test.graph.get_comb_fan_in(end)
        names = {n.path for n in fan_in if hasattr(n, "path")}
        self.assertIn("m.a", names)
        self.assertIn("m.b", names)

    def test_comb_fan_stops_at_state(self):
        code = """
        module m(input clk, input logic a, output logic x, output logic y);
            assign x = a;
            always_ff @(posedge clk)
                y <= a;
        endmodule
        """
        test = NetlistGraphTest(code)
        start = test.graph.lookup("m.a")
        fan_out = test.graph.get_comb_fan_out(start)
        names = {n.path for n in fan_out if hasattr(n, "path")}
        self.assertIn("m.x", names)
        self.assertNotIn("m.y", names)

    def test_find_nodes_wildcard(self):
        code = """
        module m(input logic a, input logic b, output logic x, output logic y);
            assign x = a;
            assign y = b;
        endmodule
        """
        test = NetlistGraphTest(code)
        all_nodes = test.graph.find_nodes("m.*")
        self.assertEqual(len(all_nodes), 4)
        none = test.graph.find_nodes("z.*")
        self.assertEqual(len(none), 0)

    def test_find_nodes_regex(self):
        code = """
        module m(input logic a, input logic b, output logic x, output logic y);
            assign x = a;
            assign y = b;
        endmodule
        """
        test = NetlistGraphTest(code)
        outputs = test.graph.find_nodes_regex(r"m\.[xy]")
        self.assertEqual(len(outputs), 2)
        none = test.graph.find_nodes_regex(r"z\..*")
        self.assertEqual(len(none), 0)


if __name__ == "__main__":
    unittest.main()
