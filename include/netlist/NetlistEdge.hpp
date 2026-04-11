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

  /// Associate a driven symbol / bit range with this edge.
  ///
  /// If the edge already carries an annotation for the same hierarchical
  /// symbol (as happens when an interval-map split causes a single contiguous
  /// source range to be re-emitted as multiple abutting intervals), widen the
  /// stored bounds to the hull of the existing and new ranges rather than
  /// overwriting. This keeps bit-level driver queries accurate without
  /// requiring multi-edges in the graph.
  auto setVariable(SymbolReference sym, DriverBitRange newBounds) {
    if (!symbol.hierarchicalPath.empty() &&
        symbol.hierarchicalPath == sym.hierarchicalPath) {
      bounds = bounds.hull(newBounds);
      return;
    }
    symbol = std::move(sym);
    bounds = newBounds;
  }

  void disable() { disabled = true; }
};

} // namespace slang::netlist
