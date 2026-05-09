#include "netlist/CombLoops.hpp"

#include "CycleDetector.hpp"

#include "netlist/NetlistEdge.hpp"

namespace slang::netlist {

namespace {

struct CombEdgePredicate {
  CombEdgePredicate() = default;
  bool operator()(const NetlistEdge &edge) {
    // Stop at the sequential boundary: edges into State are register inputs.
    return !edge.disabled && edge.getTargetNode().kind != NodeKind::State;
  }
};

} // namespace

auto CombLoops::getAllLoops() -> std::vector<std::vector<const NetlistNode *>> {
  using CycleDetectorType =
      CycleDetector<NetlistNode, NetlistEdge, CombEdgePredicate>;
  CycleDetectorType detector(netlist);
  return detector.detectCycles();
}

} // namespace slang::netlist
