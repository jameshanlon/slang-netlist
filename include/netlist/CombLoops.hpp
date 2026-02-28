#pragma once

#include "netlist/CycleDetector.hpp"
#include "netlist/FlatNetlistGraph.hpp"
#include "netlist/NetlistGraph.hpp"

namespace slang::netlist {

/// Edge predicate for combinational loop detection.
/// Passes only non-disabled, non-clocked edges.
///
/// Templated on EdgeType so it works with both NetlistEdge (live graph) and
/// FlatNetlistEdge (deserialised graph).
template <class EdgeType> struct CombEdgePredicate {
  bool operator()(const EdgeType &edge) {
    return !edge.disabled && edge.edgeKind == ast::EdgeKind::None;
  }
};

/// A class for finding combinational loops in a netlist.
///
/// Uses CycleDetector to find cycles, then reports them as combinational when
/// no edge in the cycle is clock-sensitive.
///
/// Templated on NodeType and EdgeType so it works with both NetlistGraph
/// and FlatNetlistGraph.
template <class NodeType, class EdgeType> class BasicCombLoops {
  DirectedGraph<NodeType, EdgeType> const &netlist;

public:
  explicit BasicCombLoops(DirectedGraph<NodeType, EdgeType> const &netlist)
      : netlist(netlist) {}

  auto getAllLoops() {
    using Predicate = CombEdgePredicate<EdgeType>;
    using CycleDetectorType = CycleDetector<NodeType, EdgeType, Predicate>;
    CycleDetectorType detector(netlist);
    return detector.detectCycles();
  }
};

/// CombLoops over a live NetlistGraph.
using CombLoops = BasicCombLoops<NetlistNode, NetlistEdge>;

/// CombLoops over a deserialised FlatNetlistGraph.
using FlatCombLoops = BasicCombLoops<FlatNetlistNode, FlatNetlistEdge>;

} // namespace slang::netlist
