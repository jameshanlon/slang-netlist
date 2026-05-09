#pragma once

#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistPath.hpp"

namespace slang::netlist {

/// Find a path between two points in a netlist using a depth-first
/// search.
class PathFinder {
public:
  PathFinder() = default;

  /// Find a path between two nodes in the netlist.
  /// Returns an empty NetlistPath if the path does not exist.
  auto find(NetlistNode &startNode, NetlistNode &endNode) -> NetlistPath;

  /// Find a combinatorial path between two nodes in the netlist.
  /// A combinatorial path does not pass through State nodes (ie sequential
  /// state elements). Returns an empty NetlistPath if no combinatorial
  /// path exists.
  auto findComb(NetlistNode &startNode, NetlistNode &endNode) -> NetlistPath;
};

} // namespace slang::netlist
