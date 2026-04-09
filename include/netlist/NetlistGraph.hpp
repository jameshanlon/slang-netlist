#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"
#include "netlist/TextLocation.hpp"

#include <algorithm>
#include <ranges>

namespace slang::netlist {

/// Represent the netlist connectivity of an elaborated design.
class NetlistGraph : public DirectedGraph<NetlistNode, NetlistEdge> {
public:
  FileTable fileTable;

  /// Lookup a node in the graph by its hierarchical name.
  ///
  /// @param name The hierarchical name of the node.
  /// @return A pointer to the node if found, or nullptr if not found.
  [[nodiscard]] auto lookup(std::string_view name) const -> NetlistNode *;

  /// Return a view of all nodes of the specified kind.
  ///
  /// @param kind The kind of nodes to filter.
  /// @return A view of nodes matching the specified kind.
  [[nodiscard]]
  auto filterNodes(NodeKind kind) const {
    return nodes |
           std::views::filter([kind](std::unique_ptr<NetlistNode> const &p) {
             return p->kind == kind;
           });
  }

  /// Add an edge between two nodes.
  auto addEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      -> NetlistEdge & {
    return sourceNode.addEdge(targetNode);
  }
};

} // namespace slang::netlist
