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
  /// symbol and the new range is contiguous (abutting or overlapping) with the
  /// existing one, widen the stored bounds to the union of both ranges. Returns
  /// true if the annotation was set or merged successfully, false if the edge
  /// already carries a range for the same symbol that is not contiguous with
  /// @p newBounds — the caller must create a separate edge in that case.
  auto setVariable(SymbolReference sym, DriverBitRange newBounds) -> bool {
    if (!symbol.hierarchicalPath.empty() &&
        symbol.hierarchicalPath == sym.hierarchicalPath) {
      if (bounds.isContiguousWith(newBounds)) {
        bounds = bounds.unionWith(newBounds);
        return true;
      }
      return false;
    }
    symbol = std::move(sym);
    bounds = newBounds;
    return true;
  }

  void disable() { disabled = true; }
};

} // namespace slang::netlist
