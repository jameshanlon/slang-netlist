#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"

#include <algorithm>
#include <ranges>

#if defined(SLANG_USE_THREADS)
#include <mutex>
#endif

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
#if defined(SLANG_USE_THREADS)
    std::lock_guard<std::mutex> lock(graphMutex);
#endif
    return DirectedGraph::addNode(std::move(node));
  }

  /// Thread-safe wrapper around DirectedGraph::addEdge.
  auto addEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      -> NetlistEdge & {
#if defined(SLANG_USE_THREADS)
    std::lock_guard<std::mutex> lock(graphMutex);
#endif
    return sourceNode.addEdge(targetNode);
  }

#if defined(SLANG_USE_THREADS)
private:
  mutable std::mutex graphMutex;
#endif
};

} // namespace slang::netlist
