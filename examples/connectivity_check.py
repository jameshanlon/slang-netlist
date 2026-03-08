#!/usr/bin/env python3
"""
Connectivity check using pyslang_netlist.

Demonstrates how to build a netlist from SystemVerilog source and assert
that a logical path exists between two named points in the design.
"""

import sys

import pyslang
import pyslang_netlist


class Netlist:
    """Build a netlist graph from SystemVerilog source.

    Holds references to all intermediate objects (syntax tree, compilation,
    analysis manager) to prevent them from being garbage collected while the
    graph is in use.
    """

    def __init__(self, sv_code: str):
        self.tree = pyslang.syntax.SyntaxTree.fromText(sv_code)
        self.compilation = pyslang.ast.Compilation()
        self.compilation.addSyntaxTree(self.tree)

        diagnostics = self.compilation.getAllDiagnostics()
        if len(diagnostics) > 0:
            print("Compilation errors:")
            for d in diagnostics:
                print(f"  {d}")
            sys.exit(1)
        self.compilation.freeze()

        self.analysis_manager = pyslang.analysis.AnalysisManager()
        self.analysis_manager.analyze(self.compilation)

        self.graph = pyslang_netlist.NetlistGraph()
        self.builder = pyslang_netlist.NetlistBuilder(
            self.compilation, self.analysis_manager, self.graph
        )
        self.builder.run(self.compilation)
        self.builder.finalize()

    def path_exists(self, source: str, sink: str) -> bool:
        """Return True if a logical path exists from source to sink."""
        source_node = self.graph.lookup(source)
        sink_node = self.graph.lookup(sink)
        if source_node is None:
            raise ValueError(f"Node not found: {source}")
        if sink_node is None:
            raise ValueError(f"Node not found: {sink}")
        finder = pyslang_netlist.PathFinder()
        path = finder.find(source_node, sink_node)
        return not path.empty()


def main():
    # A small design with both connected and unconnected paths.
    sv_code = r"""
    module alu(
        input  logic [7:0] a,
        input  logic [7:0] b,
        input  logic       sel,
        output logic [7:0] result,
        output logic       zero
    );
        logic [7:0] sum;
        logic [7:0] diff;

        assign sum    = a + b;
        assign diff   = a - b;
        assign result = sel ? sum : diff;
        assign zero   = (result == 8'b0);
    endmodule
    """

    nl = Netlist(sv_code)
    print(f"Netlist: {nl.graph.num_nodes()} nodes, " f"{nl.graph.num_edges()} edges\n")

    # Define connectivity assertions as (source, sink, expected) tuples.
    checks = [
        ("alu.a", "alu.result", True),  # a feeds result via sum/diff
        ("alu.b", "alu.result", True),  # b feeds result via sum/diff
        ("alu.sel", "alu.result", True),  # sel selects between sum and diff
        ("alu.a", "alu.zero", True),  # a flows through result to zero
        ("alu.b", "alu.sel", False),  # b has no path to sel
    ]

    all_passed = True
    for source, sink, expected in checks:
        connected = nl.path_exists(source, sink)
        status = "PASS" if connected == expected else "FAIL"
        direction = "->" if connected else "-/>"
        print(f"  [{status}] {source} {direction} {sink}")
        if connected != expected:
            all_passed = False

    print()
    if all_passed:
        print("All connectivity checks passed.")
    else:
        print("Some connectivity checks failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
