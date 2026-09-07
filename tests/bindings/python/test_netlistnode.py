import unittest

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
        self.assertTrue(hasattr(pyslang_netlist, "Operation"))
        self.assertTrue(hasattr(pyslang_netlist, "NodeKind"))

    def test_operation_node_kind(self):
        self.assertTrue(hasattr(pyslang_netlist.NodeKind, "Operation"))

    def test_operation_properties(self):
        for name in ("op", "width", "is_signed"):
            self.assertTrue(
                hasattr(pyslang_netlist.Operation, name),
                f"Operation is missing {name}",
            )


if __name__ == "__main__":
    unittest.main()
