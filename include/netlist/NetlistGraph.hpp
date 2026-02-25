#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"

#include <algorithm>
#include <ranges>

#include <mutex>

namespace slang::netlist {

/// Represent the netlist connectivity of an elaborated design.
class NetlistGraph : public DirectedGraph<NetlistNode, NetlistEdge> {
public:
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

  /// Thread-safe wrapper around DirectedGraph::addNode.
  auto addNode(std::unique_ptr<NetlistNode> node) -> NetlistNode & {
    std::lock_guard<std::mutex> lock(graphMutex);
    return DirectedGraph::addNode(std::move(node));
  }

  /// Thread-safe wrapper around DirectedGraph::addEdge.
  auto addEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      -> NetlistEdge & {
    std::lock_guard<std::mutex> lock(graphMutex);
    return sourceNode.addEdge(targetNode);
  }

private:
  mutable std::mutex graphMutex;
};

} // namespace slang::netlist
