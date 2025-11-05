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
        self.assertTrue(hasattr(pyslang_netlist, "NodeKind"))


if __name__ == "__main__":
    unittest.main()
