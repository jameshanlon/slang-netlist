#!/usr/bin/env python3
"""
Clock-domain map using pyslang_netlist.

For every sequential element in the design (``State`` node), look up the
clock/reset signals that gate it and group the registers by domain. Prints
a table with one row per (clock, edge) pair.

This is a first-pass clock-domain summary: it tells you how many domains
exist, how many registers each one owns, and where the clock signals
originate. Catches mistakes like an instance accidentally clocked on a
data signal, or a "single clock" design that's silently picked up two
domains because of a stray ``always @(*)`` or a typo.

Uses ``NetlistGraph.get_sensitivity(state_node)``, which returns the
deduplicated set of clocked in-edges on a ``State`` node as a list of
``(source_node, edge_kind)`` tuples. ``edge_kind`` is a
``pyslang.ast.EdgeKind``.
"""

import sys
from collections import defaultdict

import pyslang_netlist

from common import Netlist


def source_path(node) -> str:
    """
    Best-effort hierarchical path for a clock source node.
    """
    path = getattr(node, "path", None)
    if path:
        return path
    name = getattr(node, "name", None)
    if name:
        return name
    return f"<{node.kind.name}#{node.ID}>"


def clock_domain_map(graph):
    """
    Group every ``State`` node in the graph by (clock_path, edge_kind).

    Returns ``{(clock_path, edge_kind_name): [state_path, ...]}``.
    A ``State`` with no clocked in-edges is bucketed under
    ``("<unclocked>", "None")`` — typically a latch or a graph fragment
    where the sensitivity didn't survive elaboration.
    """
    domains: dict[tuple[str, str], list[str]] = defaultdict(list)
    for node in graph:
        if node.kind != pyslang_netlist.NodeKind.State:
            continue
        sensitivity = graph.get_sensitivity(node)
        if not sensitivity:
            domains[("<unclocked>", "None")].append(node.path)
            continue
        for source, edge_kind in sensitivity:
            key = (source_path(source), edge_kind.name)
            domains[key].append(node.path)
    return domains


def main():
    # Three flavours of clocking:
    #   - posedge clk + negedge rstn  -> main domain with async reset
    #   - posedge clk2                -> a second clock domain
    #   - negedge clk_n               -> a negedge-only register
    sv_code = r"""
    module dut(
        input  logic        clk,
        input  logic        clk2,
        input  logic        clk_n,
        input  logic        rstn,
        input  logic        en,
        input  logic [7:0]  din,
        input  logic [7:0]  din2,
        output logic [7:0]  q1,
        output logic [7:0]  q2,
        output logic [7:0]  q3,
        output logic        q_neg
    );
        // Main domain: posedge clk with async active-low reset.
        always_ff @(posedge clk or negedge rstn) begin
            if (!rstn) q1 <= 8'h00;
            else if (en) q1 <= din;
        end

        // Same posedge-clk domain, second flop.
        always_ff @(posedge clk or negedge rstn) begin
            if (!rstn) q2 <= 8'h00;
            else       q2 <= q1;
        end

        // Second clock domain.
        always_ff @(posedge clk2) begin
            q3 <= din2;
        end

        // Negedge-only register.
        always_ff @(negedge clk_n) begin
            q_neg <= en;
        end
    endmodule
    """

    nl = Netlist(sv_code)
    print(f"Netlist: {nl.graph.num_nodes()} nodes, " f"{nl.graph.num_edges()} edges\n")

    domains = clock_domain_map(nl.graph)

    # Stable display order: real clocks first (sorted by name), then any
    # unclocked bucket at the end.
    def sort_key(item):
        (clock, edge), _ = item
        return (clock == "<unclocked>", clock, edge)

    rows = sorted(domains.items(), key=sort_key)

    width_clock = max(len("Clock"), max(len(k[0]) for k, _ in rows))
    width_edge = max(len("Edge"), max(len(k[1]) for k, _ in rows))

    print(f"{'Clock':<{width_clock}}  {'Edge':<{width_edge}}  Registers")
    print(f"{'-' * width_clock}  {'-' * width_edge}  ---------")
    for (clock, edge), states in rows:
        print(f"{clock:<{width_clock}}  {edge:<{width_edge}}  {len(states)}")
        for path in sorted(set(states)):
            print(f"{' ' * (width_clock + width_edge + 4)}  {path}")

    # Sanity check: the design has 3 real clock domains plus one
    # unclocked latch. Each clk-driven register is sensitive to both
    # clk and rstn, so we expect the main domain to appear under both.
    expected_clocks = {
        ("dut.clk", "PosEdge"),
        ("dut.rstn", "NegEdge"),
        ("dut.clk2", "PosEdge"),
        ("dut.clk_n", "NegEdge"),
    }
    actual_clocks = {k for k in domains.keys() if k[0] != "<unclocked>"}
    if not expected_clocks.issubset(actual_clocks):
        missing = expected_clocks - actual_clocks
        print(f"\nMissing expected clock domains: {sorted(missing)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
