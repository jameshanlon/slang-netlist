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

  /// Associate a driven symbol, bit range and edge kind with this edge.
  ///
  /// One edge describes a single symbol over a single contiguous range at a
  /// single edge kind, so an annotation is only absorbed when it agrees with
  /// the stored one: same symbol, same edge kind, and a range contiguous
  /// (abutting or overlapping) with the stored range, which is widened to the
  /// union of the two. Returns true if the annotation was set or merged, and
  /// false if it disagrees, in which case the caller must put it on a parallel
  /// edge rather than overwrite what is already here.
  ///
  /// These are the same attributes NetlistGraph::mergeParallelEdges groups
  /// on, so an edge set built through this function is merged consistently.
  ///
  /// Symbol identity is by pointer: the SymbolTable interns each hierarchical
  /// path to a single canonical record, so equal paths share the same pointer.
  auto setVariable(SymbolReference const *sym, DriverBitRange newBounds,
                   ast::EdgeKind kind) -> bool {
    if (symbol == nullptr) {
      symbol = sym;
      bounds = newBounds;
      edgeKind = kind;
      return true;
    }
    if (symbol != sym || edgeKind != kind ||
        !bounds.isContiguousWith(newBounds)) {
      return false;
    }
    bounds = bounds.unionWith(newBounds);
    return true;
  }

  /// True if this edge carries a symbol annotation.
  auto hasSymbol() const -> bool { return symbol != nullptr; }

  void disable() { disabled = true; }
};

} // namespace slang::netlist
