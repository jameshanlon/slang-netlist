#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DepthFirstSearch.hpp"
#include "netlist/DirectedGraph.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>
#include <vector>

namespace slang::netlist {

/// Visitor class for identifying cycles during Depth-First Search.
template <class NodeType, class EdgeType> struct CycleDetectionVisitor {

  void visitedNode(const NodeType &node) {
    // Detect cycle: targetNode is part of the current recursion stack
    auto cycleStart =
        std::find(recursionStack.begin(), recursionStack.end(), &node);
    if (cycleStart != recursionStack.end()) {

      // Extract cycle nodes.
      std::vector<const NodeType *> cycleNodes(cycleStart,
                                               recursionStack.end());

      // Canonicalise the cycle by starting with the lowest ID.
      auto minPosition = std::min_element(
          cycleNodes.begin(), cycleNodes.end(),
          [](const NodeType *a, const NodeType *b) { return a->ID < b->ID; });
      std::rotate(cycleNodes.begin(), minPosition, cycleNodes.end());

      cycles.emplace_back(std::move(cycleNodes));
      DEBUG_PRINT("Cycle detected involving node ID {}\n", node.ID);
    }
  }

  void visitNode(const NodeType &node) {
    recursionStack.push_back(&node);
    DEBUG_PRINT("Visiting node ID {}\n", node.ID);
  }

  void visitEdge(const EdgeType &edge) {}

  void popNode() {
    if (!recursionStack.empty()) {
      recursionStack.pop_back();
    }
  }

  auto getCycles() const -> auto & { return cycles; }

  std::vector<const NodeType *> recursionStack;
  std::vector<std::vector<const NodeType *>> cycles;
};

/// Class for reporting all cycles in a directed graph.
template <class NodeType, class EdgeType, class EdgePredicate = select_all>
class CycleDetector {
public:
  using CycleType = std::vector<const NodeType *>;

  explicit CycleDetector(const DirectedGraph<NodeType, EdgeType> &graph)
      : graph(graph) {}

  /// Detect all cycles within the graph.
  /// Returns a vector containing cycles, where each cycle is represented as a
  /// vector of nodes.
  auto detectCycles() {
    std::set<CycleType> cycles;

    // Start a DFS traversal from each node
    for (const auto &nodePtr : graph) {
      CycleDetectionVisitor<NodeType, EdgeType> visitor;
      const auto *startNode = nodePtr.get();
      if (visitedNodes.count(startNode) == 0) {

        // Mark the starting node as visited.
        visitedNodes.insert(startNode);

        // Perform DFS traversal.
        DepthFirstSearch<NodeType, EdgeType,
                         CycleDetectionVisitor<NodeType, EdgeType>,
                         EdgePredicate>
            dfs(visitor, *nodePtr);

        // Additionally, mark all nodes in cycles as visited to avoid
        // redundant DFS calls.
        markAllVisitedNodes(visitor);
      }

      // Add any cycles that were found.
      cycles.insert(visitor.cycles.begin(), visitor.cycles.end());
    }

    // Return a vector.
    std::vector<CycleType> result(cycles.begin(), cycles.end());

    // Canonicalise the result by sorting by node ID.
    auto byNodeId = [](CycleType const &a, CycleType const &b) {
      return std::lexicographical_compare(
          a.begin(), a.end(), b.begin(), b.end(),
          [](auto const &pa, auto const &pb) { return pa->ID < pb->ID; });
      ;
    };
    std::sort(result.begin(), result.end(), byNodeId);

    return result;
  }

private:
  const DirectedGraph<NodeType, EdgeType> &graph;
  std::unordered_set<const NodeType *> visitedNodes;

  void markAllVisitedNodes(CycleDetectionVisitor<NodeType, EdgeType> &visitor) {
    for (const auto &cycle : visitor.getCycles()) {
      for (const auto node : cycle) {
        visitedNodes.insert(node);
      }
    }
  }
};

} // namespace slang::netlist
