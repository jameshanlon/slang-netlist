#pragma once

#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistNode.hpp"

#include <vector>

namespace slang::netlist {

/// Find combinational loops in a netlist by detecting cycles whose
/// edges are all combinational.
class CombLoops {
  NetlistGraph const &netlist;

public:
  CombLoops(NetlistGraph const &netlist) : netlist(netlist) {}

  auto getAllLoops() -> std::vector<std::vector<const NetlistNode *>>;
};

} // namespace slang::netlist
