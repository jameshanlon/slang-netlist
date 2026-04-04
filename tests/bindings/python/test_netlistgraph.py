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

    def __init__(self, code: str, *, materialize_internal_variables: bool = False):

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
        builder = pyslang_netlist.NetlistBuilder(
            self.compilation,
            self.analysis_manager,
            self.graph,
            materialize_internal_variables,
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
        self.assertEqual(node.name, "a")

    def test_find_path(self):
        code = "module m(input logic a, output logic b); assign b = a; endmodule"
        test = NetlistGraphTest(code)
        start = test.graph.lookup("m.a")
        end = test.graph.lookup("m.b")
        finder = pyslang_netlist.PathFinder()
        path = finder.find(start, end)
        self.assertTrue(path.empty() is False)

    def test_iter_nodes(self):
        code = "module m(output logic a); assign a = 1; endmodule"
        test = NetlistGraphTest(code)
        graph = test.graph
        nodes = list(graph)
        self.assertEqual(len(nodes), graph.num_nodes())
        for node in nodes:
            self.assertIsInstance(node, pyslang_netlist.NetlistNode)

    def test_bounds_and_location_metadata_are_python_safe(self):
        code = "module m(input logic [3:0] a, output logic [3:0] b); assign b = a; endmodule"
        test = NetlistGraphTest(code)
        start = test.graph.lookup("m.a")
        end = test.graph.lookup("m.b")
        self.assertEqual(start.bounds, (0, 3))
        self.assertEqual(end.bounds, (0, 3))
        self.assertEqual(
            test.graph.file_table.get_filename(start.location.file_index), "source"
        )
        self.assertGreater(start.location.line, 0)
        self.assertGreater(start.location.column, 0)

    def test_edges_expose_endpoints_and_symbol_metadata(self):
        code = "module m(input logic a, output logic b); assign b = a; endmodule"
        test = NetlistGraphTest(code)
        start = test.graph.lookup("m.a")
        path = pyslang_netlist.PathFinder().find(start, test.graph.lookup("m.b"))
        self.assertEqual(len(path), 3)

        first_edge = path[0].out_edges[0]
        self.assertIs(first_edge.source, path[0])
        self.assertIs(first_edge.target, path[1])
        self.assertEqual(first_edge.symbol_path, "m.a")
        self.assertEqual(first_edge.bounds, (0, 0))
        self.assertEqual(first_edge.edge_kind, "None")
        self.assertFalse(first_edge.disabled)
        self.assertEqual(first_edge.symbol_location.to_string(test.graph.file_table), "source:1:22")

    def test_iter_nodes_can_find_internal_materialized_variables(self):
        code = """
module m(output logic [2:0] y);
  logic [2:0] x;
  assign x = 3'b101;
  assign y = x;
endmodule
"""
        test = NetlistGraphTest(code, materialize_internal_variables=True)
        matches = [
            node
            for node in test.graph
            if isinstance(node, pyslang_netlist.Variable) and node.path == "m.x"
        ]
        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0].bounds, (0, 2))

    def test_assignment_nodes_expose_source_metadata(self):
        code = """
module m(input logic clk, a, output logic y, q);
  assign y = a;
  always_ff @(posedge clk) q <= a;
endmodule
"""
        test = NetlistGraphTest(code)
        assignments = [
            node for node in test.graph if isinstance(node, pyslang_netlist.Assignment)
        ]
        assignment_types = {node.assignment_type for node in assignments}
        self.assertIn("assign", assignment_types)
        self.assertIn("always_ff", assignment_types)

        ff_assignment = next(
            node for node in assignments if node.assignment_type == "always_ff"
        )
        self.assertFalse(ff_assignment.is_blocking)


if __name__ == "__main__":
    unittest.main()
