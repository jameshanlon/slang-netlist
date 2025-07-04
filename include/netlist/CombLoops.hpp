#pragma once

#include "CycleDetector.h"
#include "Netlist.h"
#include <algorithm>

namespace slang::netlist {

struct CombEdgePredicate {
  CombEdgePredicate() = default;
  bool operator()(const NetlistEdge &edge) {
    return !edge.disabled && edge.edgeKind == ast::EdgeKind::None;
  }
};

class CombLoops {
  Netlist const &netlist;

public:
  CombLoops(Netlist const &netlist) : netlist(netlist) {}

  auto getAllLoops() {
    using CycleDetectorType =
        CycleDetector<NetlistNode, NetlistEdge, CombEdgePredicate>;
    CycleDetectorType detector(netlist);
    return detector.detectCycles();
  }
};

} // namespace slang::netlist
