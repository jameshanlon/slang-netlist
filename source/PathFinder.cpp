#include "netlist/PathFinder.hpp"

#include "DepthFirstSearch.hpp"

#include "slang/util/Util.h"

#include <map>

namespace slang::netlist {

namespace {

using TraversalMap = std::map<NetlistNode *, NetlistNode *>;

/// Builds the parent map during DFS so the path can be reconstructed
/// from the end node back to the start.
class Visitor {
public:
  Visitor(TraversalMap &traversalMap) : traversalMap(traversalMap) {}
  void visitedNode(NetlistNode &) {}
  void visitNode(NetlistNode &) {}
  void visitEdge(NetlistEdge &edge) {
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

struct EdgePredicate {
  EdgePredicate() = default;
  bool operator()(const NetlistEdge &edge) { return !edge.disabled; }
};

/// Excludes edges targeting State nodes, restricting the search to
/// combinatorial paths only.
struct CombEdgePredicate {
  CombEdgePredicate() = default;
  bool operator()(const NetlistEdge &edge) {
    return !edge.disabled && edge.getTargetNode().kind != NodeKind::State;
  }
};

NetlistPath buildPath(TraversalMap &traversalMap, NetlistNode &startNode,
                      NetlistNode &endNode) {
  if (!traversalMap.contains(&endNode)) {
    return {};
  }
  if (startNode == endNode) {
    return NetlistPath({&endNode});
  }
  NetlistPath path;
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

} // namespace

auto PathFinder::find(NetlistNode &startNode, NetlistNode &endNode)
    -> NetlistPath {
  TraversalMap traversalMap;
  Visitor visitor(traversalMap);
  DepthFirstSearch<NetlistNode, NetlistEdge, Visitor, EdgePredicate> dfs(
      visitor, startNode);
  return buildPath(traversalMap, startNode, endNode);
}

auto PathFinder::findComb(NetlistNode &startNode, NetlistNode &endNode)
    -> NetlistPath {
  TraversalMap traversalMap;
  Visitor visitor(traversalMap);
  DepthFirstSearch<NetlistNode, NetlistEdge, Visitor, CombEdgePredicate> dfs(
      visitor, startNode);
  return buildPath(traversalMap, startNode, endNode);
}

} // namespace slang::netlist
