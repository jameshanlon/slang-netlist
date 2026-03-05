#pragma once

#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverBitRange.hpp"
#include "netlist/SymbolReference.hpp"

#include "slang/ast/SemanticFacts.h"

namespace slang::netlist {

class NetlistNode;

/// A class representing a dependency between two nodes in the netlist.
class NetlistEdge : public DirectedEdge<NetlistNode, NetlistEdge> {
public:
  ast::EdgeKind edgeKind{ast::EdgeKind::None};
  SymbolReference symbol;
  DriverBitRange bounds;
  bool disabled{false};

  NetlistEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      : DirectedEdge(sourceNode, targetNode) {}

  auto setEdgeKind(ast::EdgeKind kind) { this->edgeKind = kind; }

  auto setVariable(SymbolReference sym, DriverBitRange bounds) {
    this->symbol = std::move(sym);
    this->bounds = bounds;
  }

  void disable() { disabled = true; }
};

} // namespace slang::netlist
