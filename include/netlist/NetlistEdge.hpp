#pragma once

#include "netlist/DirectedGraph.hpp"

#include "slang/analysis/ValueDriver.h"

namespace slang::netlist {

class NetlistNode;

/// A class representing a dependency between two nodes in the netlist.
class NetlistEdge : public DirectedEdge<NetlistNode, NetlistEdge> {
public:
  ast::Symbol const *symbol{nullptr};
  std::pair<uint64_t, uint64_t> bounds;
  bool disabled{false};

  NetlistEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      : DirectedEdge(sourceNode, targetNode) {}

  auto setVariable(ast::Symbol const *symbol,
                   std::pair<uint64_t, uint64_t> bounds) {
    this->symbol = symbol;
    this->bounds = bounds;
  }

  void disable() { disabled = true; }
};

} // namespace slang::netlist
