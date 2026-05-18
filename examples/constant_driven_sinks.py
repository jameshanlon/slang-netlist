#!/usr/bin/env python3
"""
Constant-driven sink finder using pyslang_netlist.

Walks the netlist looking for ``Variable`` or output ``Port`` sinks whose
fan-in only contains literal constants. These are tied-off / dead signals:
either intentional (a parameter overridden to a constant, an unused output
strapped low) or a leftover from refactoring. Either way they're worth a
look, and they're invisible to a plain syntax search because the constant
might be several levels of ``assign`` upstream.

Note that the builder elides intermediate ``Variable`` nodes that only
forward a single driver; only ``Port`` nodes and any ``Variable`` that
survives that simplification appear in the result. A chained tie like
``logic w; assign w = 1'b0; assign y = w;`` still resolves correctly —
the fan-in of ``y`` reaches the constant through the elided ``w``.

Uses ``NetlistGraph.get_comb_fan_in(sink)``, which returns the sink itself
plus every node reachable via combinational edges in the backward
direction. Traversal stops at ``State`` nodes (they appear in the result
but their own fan-in is not explored). The classification rule:

* fan-in contains at least one ``Constant``;
* fan-in contains no ``State`` (the sink would depend on a register);
* every ``Port`` in fan-in is itself driven — an undriven port is a
  top-level input, i.e. a real external signal flowing in.

That keeps the answer precise: ``assign y = a & 1'b0`` is *not* flagged
even though ``a`` is gated by zero, because the graph faithfully records
that ``a`` reaches ``y``. The example focuses on sinks whose every input
is a constant.
"""

import sys
from collections import defaultdict

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


def is_constant_driven(graph, sink) -> tuple[bool, list]:
    """
    Decide whether ``sink``'s only inputs come from literal constants.

    Returns ``(is_const, constants)``. ``constants`` is the list of
    ``Constant`` nodes feeding the sink (may be empty even when
    ``is_const`` is False, e.g. for a sink with no fan-in at all).
    """
    fan_in = graph.get_comb_fan_in(sink)

    constants = []
    saw_state = False
    saw_real_input = False

    for n in fan_in:
        if n is sink:
            continue
        kind = n.kind
        if kind == pyslang_netlist.NodeKind.Constant:
            constants.append(n)
        elif kind == pyslang_netlist.NodeKind.State:
            saw_state = True
        elif kind == pyslang_netlist.NodeKind.Port:
            # Ports appearing in the fan-in are either pass-throughs (their
            # own driver is also in the fan-in) or top-level inputs (no
            # driver). The latter mean the sink depends on a real external
            # signal, not just constants.
            if not n.is_driven():
                saw_real_input = True

    is_const = bool(constants) and not saw_state and not saw_real_input
    return is_const, constants


def find_constant_driven_sinks(graph):
    """
    Return every constant-driven Variable / output Port in the graph.

    Result: ``[(sink_path, [constant_value, ...]), ...]`` sorted by path.
    The constant values are deduplicated and sorted for stable output.
    """
    results = []
    seen_paths = defaultdict(set)
    for node in graph:
        if node.kind == pyslang_netlist.NodeKind.Variable:
            sink_path = node.path
        elif node.kind == pyslang_netlist.NodeKind.Port and node.is_output():
            sink_path = node.path
        else:
            continue

        is_const, constants = is_constant_driven(graph, node)
        if not is_const:
            continue

        values = {c.value for c in constants}
        # A wide signal can show up as multiple Variable/Port slice nodes
        # under the same hierarchical path; merge their constants.
        seen_paths[sink_path] |= values

    for path in sorted(seen_paths):
        results.append((path, sorted(seen_paths[path])))
    return results


def main():
    sv_code = r"""
    module leaf(
        input  logic       in_a,
        input  logic [3:0] in_data,
        output logic       out_real,    // driven by real input
        output logic       out_tied_hi, // tied to 1
        output logic [3:0] out_tied_z   // tied to a wider constant
    );
        assign out_real    = in_a;
        assign out_tied_hi = 1'b1;
        assign out_tied_z  = 4'h0;
    endmodule

    module top(
        input  logic       in_a,
        input  logic [3:0] in_data,
        output logic       y_real,
        output logic       y_tied,
        output logic       y_mixed,
        output logic       y_chained,
        output logic       y_leaf_tied
    );
        logic const_var;     // driven only by 1'b0
        logic pass_through;  // driven by const_var (transitive)
        logic mixed;         // mix of real and constant -> NOT tied

        assign const_var    = 1'b0;
        assign pass_through = const_var;
        assign mixed        = in_a & 1'b1;  // in_a still in fan-in

        assign y_real    = in_a;
        assign y_tied    = const_var;
        assign y_mixed   = mixed;
        assign y_chained = pass_through;

        // Pull one of the leaf module's tied outputs up to top so it
        // shows up as a constant-driven sink at the top level too.
        leaf u_leaf(
            .in_a(in_a),
            .in_data(in_data),
            .out_real(),
            .out_tied_hi(y_leaf_tied),
            .out_tied_z()
        );
    endmodule
    """

    nl = Netlist(sv_code)
    print(f"Netlist: {nl.graph.num_nodes()} nodes, " f"{nl.graph.num_edges()} edges\n")

    sinks = find_constant_driven_sinks(nl.graph)
    print(f"Constant-driven sinks ({len(sinks)}):")
    width = max((len(p) for p, _ in sinks), default=0)
    for path, values in sinks:
        joined = ", ".join(values)
        print(f"  {path:<{width}}  <- {{{joined}}}")

    # Sanity check: the design has these intentional ties at port
    # boundaries. Intermediate Variables (const_var, pass_through) are
    # elided by the builder, so they don't appear as separate sinks;
    # the chain through them still resolves to a constant at y_chained.
    # `y_real` and `y_mixed` must NOT appear -- both depend on `in_a`.
    expected = {
        "top.y_tied",
        "top.y_chained",
        "top.y_leaf_tied",
        "top.u_leaf.out_tied_hi",
        "top.u_leaf.out_tied_z",
    }
    actual = {p for p, _ in sinks}
    forbidden = {"top.y_real", "top.y_mixed", "top.u_leaf.out_real"} & actual
    if forbidden:
        print(f"\nNon-constant sinks incorrectly flagged: {sorted(forbidden)}")
        sys.exit(1)
    missing = expected - actual
    if missing:
        print(f"\nExpected tied-off sinks not found: {sorted(missing)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
