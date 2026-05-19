"""
Shared setup for the pyslang_netlist examples.
"""

import sys

import pyslang
import pyslang_netlist


class Netlist:
    """
    Build a netlist graph from SystemVerilog source.

    Hold references to the syntax tree, compilation, and analysis
    manager to keep them alive while ``graph`` is in use.
    """

    def __init__(self, sv_code: str):
        self.tree = pyslang.syntax.SyntaxTree.fromText(sv_code)
        self.compilation = pyslang.ast.Compilation()
        self.compilation.addSyntaxTree(self.tree)

        # Skip warnings (e.g. ``EmptyInputPortConn`` is exactly the case
        # the ``unconnected_inputs`` example wants to detect). Real errors
        # still abort.
        for d in self.compilation.getAllDiagnostics():
            if d.isError():
                print(f"Compilation error: {d}")
                sys.exit(1)

        pyslang_netlist.VisitAll().run(self.compilation)
        self.compilation.freeze()

        self.analysis_manager = pyslang.analysis.AnalysisManager()
        self.analysis_manager.analyze(self.compilation)

        self.compilation.unfreeze()

        self.graph = pyslang_netlist.NetlistGraph()
        self.graph.build(self.compilation, self.analysis_manager)
