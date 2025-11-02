#pragma once

#include "netlist/DepthFirstSearch.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistPath.hpp"

#include "slang/util/Util.h"

#include <map>
#include <vector>

namespace slang::netlist {

/// Find a path between two points in a netlist.
///
/// This class uses a depth-first search to find a path between two nodes in the
/// netlist graph. It constructs a traversal map that captures the parent-child
/// relationships between nodes, allowing it to reconstruct the path from the
/// end node back to the start node.
class PathFinder {
private:
  /// Depth-first traversal produces a tree sub graph and as such, each node
  /// can only have one parent node. This map captures these relationships and
  /// is used to determine paths between leaf nodes and the root node of the
  /// tree.
  using TraversalMap = std::map<NetlistNode *, NetlistNode *>;

  /// A visitor for the search that constructs the traversal map.
  class Visitor {
  public:
    Visitor(NetlistBuilder const &netlist, TraversalMap &traversalMap)
        : netlist(netlist), traversalMap(traversalMap) {}
    void visitedNode(NetlistNode &node) {}
    void visitNode(NetlistNode &node) {}
    void visitEdge(NetlistEdge &edge) {
      auto *sourceNode = &edge.getSourceNode();
      auto *targetNode = &edge.getTargetNode();
      SLANG_ASSERT(traversalMap.count(targetNode) == 0 &&
                   "node cannot have two parents");
      traversalMap[targetNode] = sourceNode;
    }

  private:
    NetlistBuilder const &netlist;
    TraversalMap &traversalMap;
  };

  /// A selector for edges that can be traversed in the search.
  struct EdgePredicate {
    EdgePredicate() = default;
    bool operator()(const NetlistEdge &edge) { return !edge.disabled; }
  };

  NetlistPath buildPath(TraversalMap &traversalMap, NetlistNode &startNode,
                        NetlistNode &endNode) {
    // Empty path.
    if (!traversalMap.contains(&endNode)) {
      return {};
    }
    // Single-node path.
    if (startNode == endNode) {
      return NetlistPath({&endNode});
    }
    // Multi-node path.
    NetlistPath path;
    auto *nextNode = &endNode;
    path.add(endNode);
    do {
      nextNode = traversalMap[nextNode];
      // Add the node to the path.
      SLANG_ASSERT(nextNode != nullptr);
      path.add(*nextNode);
    } while (nextNode != &startNode);
    path.reverse();
    return path;
  }

public:
  PathFinder(NetlistBuilder const &netlist) : netlist(netlist) {}

  /// Find a path between two nodes in the netlist.
  /// Return a NetlistPath object that is empty if the path does not exist.
  auto find(NetlistNode &startNode, NetlistNode &endNode) -> NetlistPath {
    TraversalMap traversalMap;
    Visitor visitor(netlist, traversalMap);
    DepthFirstSearch<NetlistNode, NetlistEdge, Visitor, EdgePredicate> dfs(
        visitor, startNode);
    return buildPath(traversalMap, startNode, endNode);
  }

private:
  NetlistBuilder const &netlist;
};

} // namespace slang::netlist
