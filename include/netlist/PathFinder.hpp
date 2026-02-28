#pragma once

#include "netlist/DepthFirstSearch.hpp"
#include "netlist/FlatNetlistGraph.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistPath.hpp"

#include "slang/util/Util.h"

#include <map>

namespace slang::netlist {

/// Find a path between two points in a directed graph.
///
/// This class uses a depth-first search to find a path between two nodes. It
/// constructs a traversal map capturing the parent-child relationships between
/// nodes, allowing it to reconstruct the path from the end node back to the
/// start node.
///
/// Templated on NodeType and EdgeType so the same implementation works for
/// both the live NetlistGraph and the deserialised FlatNetlistGraph.
template <class NodeType, class EdgeType> class BasicPathFinder {
private:
  /// Depth-first traversal produces a tree sub-graph; each node can have only
  /// one parent. This map captures those relationships and is used to
  /// determine paths between leaf nodes and the root.
  using TraversalMap = std::map<NodeType *, NodeType *>;

  /// A visitor that builds the traversal map.
  class Visitor {
  public:
    Visitor(TraversalMap &traversalMap) : traversalMap(traversalMap) {}

    void visitedNode(NodeType &node) {}
    void visitNode(NodeType &node) {}

    void visitEdge(EdgeType &edge) {
      auto *sourceNode = &edge.getSourceNode();
      auto *targetNode = &edge.getTargetNode();
      SLANG_ASSERT(traversalMap.count(targetNode) == 0 &&
                   "node cannot have two parents");
      traversalMap[targetNode] = sourceNode;
    }

    void popNode() {}

  private:
    TraversalMap &traversalMap;
  };

  /// A selector that skips disabled edges.
  struct EdgePredicate {
    bool operator()(const EdgeType &edge) { return !edge.disabled; }
  };

  auto buildPath(TraversalMap &traversalMap, NodeType &startNode,
                 NodeType &endNode) -> BasicPath<NodeType> {
    // Empty path.
    if (!traversalMap.contains(&endNode)) {
      return {};
    }
    // Single-node path.
    if (startNode == endNode) {
      return BasicPath<NodeType>({&endNode});
    }
    // Multi-node path.
    BasicPath<NodeType> path;
    auto *nextNode = &endNode;
    path.add(endNode);
    do {
      nextNode = traversalMap[nextNode];
      SLANG_ASSERT(nextNode != nullptr);
      path.add(*nextNode);
    } while (nextNode != &startNode);
    path.reverse();
    return path;
  }

public:
  /// Find a path between two nodes.
  /// Returns a BasicPath that is empty if no path exists.
  auto find(NodeType &startNode, NodeType &endNode) -> BasicPath<NodeType> {
    TraversalMap traversalMap;
    Visitor visitor(traversalMap);
    DepthFirstSearch<NodeType, EdgeType, Visitor, EdgePredicate> dfs(visitor,
                                                                     startNode);
    return buildPath(traversalMap, startNode, endNode);
  }
};

/// PathFinder over a live NetlistGraph.
using PathFinder = BasicPathFinder<NetlistNode, NetlistEdge>;

/// PathFinder over a deserialised FlatNetlistGraph.
using FlatPathFinder = BasicPathFinder<FlatNetlistNode, FlatNetlistEdge>;

} // namespace slang::netlist
