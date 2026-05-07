#!/usr/bin/env python3
"""
Unconnected input port check using pyslang_netlist.

Demonstrates two related checks on the netlist graph:

1. **Whole-port unconnected.** An instance input port that is not driven
   at all (e.g. ``.in()`` left empty, or omitted from a positional
   connection list).

2. **Partially undriven port.** A wider input port where only some bits
   are driven (e.g. ``.data({dangling, in_lo})`` where ``dangling`` is
   never assigned).

Both checks use ``Port.is_driven()``: an O(1) check that returns True
when something in the graph feeds the port. A port slice with no
external driver returns False here.

Top-level module inputs are graph sources by design, so they always look
"unconnected" by the same rule. The example takes the top module name as
an argument and skips ports directly under it.
"""

import sys

import pyslang
import pyslang_netlist


class Netlist:
    """
    Build a netlist graph from SystemVerilog source.

    Holds references to all intermediate objects (syntax tree, compilation,
    analysis manager) to prevent them from being garbage collected while the
    graph is in use.
    """

    def __init__(self, sv_code: str):
        self.tree = pyslang.syntax.SyntaxTree.fromText(sv_code)
        self.compilation = pyslang.ast.Compilation()
        self.compilation.addSyntaxTree(self.tree)

        # ``EmptyInputPortConn`` is just a warning emitted for the explicit
        # ``.port()`` form; it's exactly the case this example detects, so
        # don't bail on it. Real errors still abort.
        for d in self.compilation.getAllDiagnostics():
            if d.isError():
                print(f"Compilation error: {d}")
                sys.exit(1)

        pyslang_netlist.VisitAll().run(self.compilation)
        self.compilation.freeze()

        self.analysis_manager = pyslang.analysis.AnalysisManager()
        self.analysis_manager.analyze(self.compilation)

        pyslang_netlist.unfreeze_compilation(self.compilation)

        self.graph = pyslang_netlist.NetlistGraph()
        self.graph.build(self.compilation, self.analysis_manager)


def find_unconnected_inputs(graph, top_module: str):
    """
    Return the input ``Port`` nodes that have no driver in the graph.

    Top-level module inputs (direct children of ``top_module``) are
    skipped: they are graph sources and look "unconnected" by definition.
    """
    prefix = top_module + "."
    unconnected = []
    for node in graph:
        if node.kind != pyslang_netlist.NodeKind.Port:
            continue
        if not node.is_input():
            continue
        # Skip ports that sit directly under the top module: those are
        # the design's external inputs.
        if node.path.startswith(prefix) and "." not in node.path[len(prefix) :]:
            continue
        if not node.is_driven():
            unconnected.append(node)
    return unconnected


def find_undriven_bit_ranges(graph, top_module: str):
    """
    Return undriven bit ranges of every instance input port.

    Each undriven slice corresponds to a ``Port`` node in the graph: the
    netlist builder splits a wide port across multiple slice nodes when
    it's only partially connected. We pick out the slices whose fan-in
    is just themselves and read the bit range straight off ``bounds``,
    then merge adjacent slices that meet at the same port.

    Result: ``[(port_path, [(lo, hi), ...]), ...]`` — one entry per
    instance input port that has any undriven bits. A port that is fully
    unconnected appears here too, with a single range covering its width.

    Top-level module input ports are skipped (they are graph sources).
    """
    prefix = top_module + "."
    by_path = {}
    for node in graph:
        if node.kind != pyslang_netlist.NodeKind.Port:
            continue
        if not node.is_input():
            continue
        if node.path.startswith(prefix) and "." not in node.path[len(prefix) :]:
            continue
        if node.is_driven():
            continue
        lo, hi = node.bounds
        by_path.setdefault(node.path, []).append((lo, hi))

    results = []
    for path in sorted(by_path):
        ranges = sorted(by_path[path])
        merged = [ranges[0]]
        for lo, hi in ranges[1:]:
            mlo, mhi = merged[-1]
            if lo <= mhi + 1:
                merged[-1] = (mlo, max(mhi, hi))
            else:
                merged.append((lo, hi))
        results.append((path, merged))
    return results


def main():
    # ``top.u_sub.c`` is left explicitly unconnected. ``top.u_other.b`` is
    # omitted from a named connection list, which has the same effect.
    # ``top.u_wide.data`` is partially driven: bits [3:0] come from
    # ``in_lo``, while bits [7:4] are driven by ``dangling`` which is
    # never assigned anywhere.
    sv_code = r"""
    module sub(
        input  logic a,
        input  logic b,
        input  logic c,
        output logic y
    );
        assign y = a & b & c;
    endmodule

    module wide_sub(
        input  logic [7:0] data,
        output logic       y
    );
        assign y = ^data;
    endmodule

    module top(
        input  logic       in_a,
        input  logic       in_b,
        input  logic [3:0] in_lo,
        output logic       out_x,
        output logic       out_y,
        output logic       out_z
    );
        logic [3:0] dangling;

        sub u_sub  (.a(in_a), .b(in_b), .c(),     .y(out_x));
        sub u_other(.a(in_a),           .c(in_b), .y(out_y));
        wide_sub u_wide(.data({dangling, in_lo}), .y(out_z));
    endmodule
    """

    nl = Netlist(sv_code)
    print(f"Netlist: {nl.graph.num_nodes()} nodes, " f"{nl.graph.num_edges()} edges\n")

    # Simple check: every Port slice with no driver. Wide ports that are
    # only partially connected appear here too — the slice node by itself
    # doesn't tell you which bits it covers.
    unconnected = find_unconnected_inputs(nl.graph, top_module="top")
    print(f"Undriven port slice nodes ({len(unconnected)}):")
    for port in unconnected:
        print(f"  {port.path}")

    # Detailed check: per-bit, with bit ranges grouped per port path.
    undriven = find_undriven_bit_ranges(nl.graph, top_module="top")
    print(f"\nUndriven bit ranges ({len(undriven)} port(s)):")
    for path, ranges in undriven:
        slices = ", ".join(
            f"[{lo}]" if lo == hi else f"[{hi}:{lo}]" for lo, hi in ranges
        )
        print(f"  {path} {slices}")

    # The example design intentionally has three problem ports.
    expected_unconnected = {"top.u_sub.c", "top.u_other.b", "top.u_wide.data"}
    expected_undriven = {
        ("top.u_sub.c", ((0, 0),)),
        ("top.u_other.b", ((0, 0),)),
        ("top.u_wide.data", ((4, 7),)),
    }
    actual_unconnected = {p.path for p in unconnected}
    actual_undriven = {(p, tuple(r)) for p, r in undriven}
    if actual_unconnected != expected_unconnected:
        print(
            f"\nExpected unconnected {sorted(expected_unconnected)}, "
            f"got {sorted(actual_unconnected)}"
        )
        sys.exit(1)
    if actual_undriven != expected_undriven:
        print(
            f"\nExpected undriven {sorted(expected_undriven)}, "
            f"got {sorted(actual_undriven)}"
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
