#pragma once

#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverBitRange.hpp"
#include "netlist/SymbolReference.hpp"

#include "slang/ast/SemanticFacts.h"

namespace slang::netlist {

class NetlistNode;

/// A class representing a dependency between two nodes in the netlist.
///
/// The driven symbol annotation is stored as a pointer into the owning
/// NetlistGraph's SymbolTable so that many edges referring to the same
/// hierarchical symbol share a single backing record.
class NetlistEdge : public DirectedEdge<NetlistNode, NetlistEdge> {
public:
  ast::EdgeKind edgeKind{ast::EdgeKind::None};
  SymbolReference const *symbol{nullptr};
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
  ///
  /// Symbol identity is by pointer: the SymbolTable interns each hierarchical
  /// path to a single canonical record, so equal paths share the same pointer.
  auto setVariable(SymbolReference const *sym, DriverBitRange newBounds)
      -> bool {
    if (symbol != nullptr && symbol == sym) {
      if (bounds.isContiguousWith(newBounds)) {
        bounds = bounds.unionWith(newBounds);
        return true;
      }
      return false;
    }
    symbol = sym;
    bounds = newBounds;
    return true;
  }

  /// True if this edge carries a symbol annotation.
  auto hasSymbol() const -> bool { return symbol != nullptr; }

  void disable() { disabled = true; }
};

} // namespace slang::netlist
