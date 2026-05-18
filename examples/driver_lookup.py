#!/usr/bin/env python3
"""
Driver lookup by bit range using pyslang_netlist.

Answers "who writes bits [hi:lo] of <hierarchical signal>?" — the typical
question when chasing down a multi-driver signal or trying to understand
which always_ff block is responsible for a specific bit of a register.

The slang-netlist builder resolves assignments and port connections at
bit granularity, so a wide signal whose slices come from different
sources is represented by separate edges per slice. ``get_drivers`` does
the lookup: it returns every source node that drives an edge whose
``symbol_path`` is the queried name and whose bit range overlaps the
query range.

The example builds a small design with three patterns:

* ``mixed`` — bottom and top halves driven by separate ``assign``
  statements.
* ``concat`` — driven by a single concat (``{hi, lo}``) which the
  builder splits into two per-slice edges.
* ``reg_q`` — a register where different branches of an ``always_ff``
  write different bit ranges, so different bit ranges resolve to
  different sources.
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


def describe(node) -> str:
    """One-line tag for a driver node, including hierarchical path when known."""
    kind = node.kind.name
    path = getattr(node, "path", None)
    name = getattr(node, "name", None)
    if path:
        return f"{kind}({path})"
    if name:
        return f"{kind}({name})"
    return f"{kind}#{node.ID}"


def print_drivers(graph, signal: str, lo: int, hi: int):
    """Look up drivers for ``signal[hi:lo]`` and print them."""
    drivers = graph.get_drivers(signal, lo, hi)
    label = f"{signal}[{hi}:{lo}]" if hi != lo else f"{signal}[{lo}]"
    if not drivers:
        print(f"  {label:<28} (no drivers)")
        return
    # A wide signal can have multiple State (or Assignment) slice nodes
    # sharing one hierarchical path. Dedupe by the printable tag so the
    # output stays readable.
    tags = sorted({describe(d) for d in drivers})
    print(f"  {label:<28} <- {', '.join(tags)}")


def main():
    sv_code = r"""
    module top(
        input  logic [3:0] a,
        input  logic [3:0] b,
        input  logic       sel,
        input  logic       clk,
        input  logic       rstn,
        output logic [7:0] mixed,
        output logic [7:0] concat,
        output logic [7:0] reg_q
    );
        // Two separate assigns into disjoint halves of `mixed`.
        assign mixed[3:0] = a;
        assign mixed[7:4] = b;

        // Concat into `concat`: builder splits this into two per-slice
        // edges, one for each operand of the {b, a}.
        assign concat = {b, a};

        // Register with branch-dependent bit ranges. `reg_q[3:0]` and
        // `reg_q[7:4]` are written in different branches of the ff.
        always_ff @(posedge clk or negedge rstn) begin
            if (!rstn)    reg_q <= 8'h00;
            else if (sel) reg_q[3:0] <= a;
            else          reg_q[7:4] <= b;
        end
    endmodule
    """

    nl = Netlist(sv_code)
    print(f"Netlist: {nl.graph.num_nodes()} nodes, " f"{nl.graph.num_edges()} edges\n")

    # `mixed`: two disjoint halves -> the two halves resolve to different
    # drivers; a query covering both halves returns both.
    print("mixed:")
    print_drivers(nl.graph, "top.mixed", 0, 3)
    print_drivers(nl.graph, "top.mixed", 4, 7)
    print_drivers(nl.graph, "top.mixed", 0, 7)
    print_drivers(nl.graph, "top.mixed", 2, 5)  # straddles the boundary
    print()

    # `concat`: one assign, but the builder makes one edge per operand of
    # the concat. So the two halves still resolve to distinct slice edges
    # (the source node may or may not be the same Assignment node,
    # depending on how the builder structured them).
    print("concat (built via {b, a}):")
    print_drivers(nl.graph, "top.concat", 0, 3)
    print_drivers(nl.graph, "top.concat", 4, 7)
    print()

    # `reg_q`: a registered signal. The drivers are the State node and
    # the branches of the always_ff that write into it. Different bit
    # ranges resolve to different upstream sources.
    print("reg_q (registered, branch-conditional bit slices):")
    print_drivers(nl.graph, "top.reg_q", 0, 0)  # one bit
    print_drivers(nl.graph, "top.reg_q", 0, 3)  # bottom half
    print_drivers(nl.graph, "top.reg_q", 4, 7)  # top half
    print_drivers(nl.graph, "top.reg_q", 0, 7)  # whole word

    # Sanity check on the headline case: the bottom half of `mixed`
    # should NOT have any driver in common with the top half (different
    # `assign` statements compile to different source nodes).
    bot = {d.ID for d in nl.graph.get_drivers("top.mixed", 0, 3)}
    top = {d.ID for d in nl.graph.get_drivers("top.mixed", 4, 7)}
    if not bot or not top:
        print("\nExpected drivers on both halves of `mixed`.")
        sys.exit(1)
    if bot & top:
        print(f"\nUnexpected shared driver between halves: {sorted(bot & top)}")
        sys.exit(1)
    # And straddling [2:5] should hit both.
    straddle = {d.ID for d in nl.graph.get_drivers("top.mixed", 2, 5)}
    if not (bot.issubset(straddle) and top.issubset(straddle)):
        print("\nStraddling query should return drivers from both halves.")
        sys.exit(1)


if __name__ == "__main__":
    main()
