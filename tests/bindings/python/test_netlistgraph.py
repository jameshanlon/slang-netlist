import unittest

import pyslang_netlist


class TestNetlistGraph(unittest.TestCase):
    def test_netlistgraph_import(self):
        self.assertTrue(hasattr(pyslang_netlist, "NetlistGraph"))

    def test_netlistgraph_constructor(self):
        graph = pyslang_netlist.NetlistGraph()
        self.assertIsInstance(graph, pyslang_netlist.NetlistGraph)

    def test_netlistgraph_lookup(self):
        graph = pyslang_netlist.NetlistGraph()
        # Should return None for any name in an empty graph.
        self.assertIsNone(graph.lookup("nonexistent"))


if __name__ == "__main__":
    unittest.main()
