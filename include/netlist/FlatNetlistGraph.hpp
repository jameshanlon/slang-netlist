#pragma once

#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverBitRange.hpp"
#include "netlist/NetlistNode.hpp"

#include "slang/ast/SemanticFacts.h"
#include "slang/ast/symbols/PortSymbols.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>

namespace slang::netlist {

class FlatNetlistEdge;

/// An AST-free node in the netlist, populated from deserialised JSON.
///
/// Holds the same structural information as NetlistNode subclasses (ID, kind,
/// hierarchical path, bit range, port direction) but carries no references into
/// a live slang compilation, making it safe to construct without the AST.
class FlatNetlistNode : public Node<FlatNetlistNode, FlatNetlistEdge> {
public:
  size_t ID;
  NodeKind kind;

  // Populated for Port, Variable, and State nodes.
  std::string hierarchicalPath;
  std::string name;
  std::pair<int32_t, int32_t> bounds{0, 0};

  // Populated for Port nodes only.
  std::optional<ast::ArgumentDirection> direction;

  FlatNetlistNode(size_t id, NodeKind kind) : ID(id), kind(kind) {}
};

/// An AST-free edge in the netlist, populated from deserialised JSON.
class FlatNetlistEdge : public DirectedEdge<FlatNetlistNode, FlatNetlistEdge> {
public:
  ast::EdgeKind edgeKind{ast::EdgeKind::None};
  std::string symbolName;
  std::pair<int32_t, int32_t> bounds{0, 0};
  bool disabled{false};

  FlatNetlistEdge(FlatNetlistNode &sourceNode, FlatNetlistNode &targetNode)
      : DirectedEdge(sourceNode, targetNode) {}
};

/// A netlist graph populated from a deserialised JSON snapshot.
///
/// Provides the same lookup and filter interface as NetlistGraph, so the
/// generic analysis tools (BasicPathFinder, BasicCombLoops, etc.) can be
/// instantiated directly over this type.
class FlatNetlistGraph
    : public DirectedGraph<FlatNetlistNode, FlatNetlistEdge> {
public:
  /// Lookup a node by its hierarchical path (the same key used by
  /// NetlistGraph::lookup).
  [[nodiscard]] auto lookup(std::string_view name) const -> FlatNetlistNode * {
    auto it = std::ranges::find_if(nodes, [name](auto const &node) {
      return node->hierarchicalPath == name;
    });
    return it != nodes.end() ? it->get() : nullptr;
  }

  /// Return a view of all nodes of the specified kind.
  [[nodiscard]] auto filterNodes(NodeKind kind) const {
    return nodes | std::views::filter(
                       [kind](std::unique_ptr<FlatNetlistNode> const &p) {
                         return p->kind == kind;
                       });
  }
};

} // namespace slang::netlist
