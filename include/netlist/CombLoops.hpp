#pragma once

#include "netlist/CycleDetector.hpp"
#include "netlist/NetlistGraph.hpp"

namespace slang::netlist {

struct CombEdgePredicate {
  CombEdgePredicate() = default;
  bool operator()(const NetlistEdge &edge) {
    return !edge.disabled && edge.edgeKind == ast::EdgeKind::None;
  }
};

/// A class for finding combinational loops in a netlist.
///
/// Use CycleDetector to find cycles in the netlist graph, then
/// reports loops as combinatorial when there are no edges are a edge sensitive.
class CombLoops {
  NetlistGraph const &netlist;

public:
  CombLoops(NetlistGraph const &netlist) : netlist(netlist) {}

  auto getAllLoops() {
    using CycleDetectorType =
        CycleDetector<NetlistNode, NetlistEdge, CombEdgePredicate>;
    CycleDetectorType detector(netlist);
    return detector.detectCycles();
  }
};

} // namespace slang::netlist
