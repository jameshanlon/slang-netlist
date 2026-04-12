#pragma once

#include <set>
#include <vector>

namespace slang::netlist {

/// Direction of traversal for depth-first search.
enum class Direction { Forward, Backward };

/// A predicate for selecting edges in a depth-first search.
struct select_all {
  template <typename T> auto operator()(const T &) const -> bool {
    return true;
  }
};

/// Depth-first search on a directed graph. A visitor class provides visibility
/// to the caller of visits to edges and nodes. An optional edge predicate
/// selects which edges can be included in the traversal. The Direction
/// parameter controls whether the search follows outgoing (Forward) or
/// incoming (Backward) edges.
template <class NodeType, class EdgeType, class Visitor,
          class EdgePredicate = select_all, Direction Dir = Direction::Forward>
class DepthFirstSearch {
public:
  DepthFirstSearch(Visitor &visitor, NodeType &startNode) : visitor(visitor) {
    setup(startNode);
    run();
  }

  DepthFirstSearch(Visitor &visitor, EdgePredicate edgePredicate,
                   NodeType &startNode)
      : visitor(visitor), edgePredicate(edgePredicate) {
    setup(startNode);
    run();
  }

private:
  using EdgeIteratorType = typename NodeType::iterator;
  using VisitStackElement = std::pair<NodeType &, EdgeIteratorType>;

  static auto edgeBegin(NodeType &node) -> EdgeIteratorType {
    if constexpr (Dir == Direction::Forward)
      return node.begin();
    else
      return node.inBegin();
  }

  static auto edgeEnd(NodeType &node) -> EdgeIteratorType {
    if constexpr (Dir == Direction::Forward)
      return node.end();
    else
      return node.inEnd();
  }

  static auto &nextNode(EdgeType &edge) {
    if constexpr (Dir == Direction::Forward)
      return edge.getTargetNode();
    else
      return edge.getSourceNode();
  }

  /// Setup the traversal.
  void setup(NodeType &startNode) {
    visitedNodes.insert(&startNode);
    visitStack.push_back(VisitStackElement(startNode, edgeBegin(startNode)));
    visitor.visitNode(startNode);
  }

  /// Perform a depth-first traversal, calling the visitor methods on the way.
  void run() {
    while (!visitStack.empty()) {
      auto &node = visitStack.back().first;
      auto &nodeIt = visitStack.back().second;
      // Visit each child node that hasn't already been visited.
      while (nodeIt != edgeEnd(node)) {
        auto *edge = nodeIt->get();
        auto &target = nextNode(*edge);
        nodeIt++;
        if (!edgePredicate(*edge)) {
          // Skip this edge.
          continue;
        }

        if (visitedNodes.count(&target) != 0) {
          // This node has already been visited.
          visitor.visitedNode(target);
        } else {
          // Push a new 'current' node onto the stack and mark it as
          // visited.
          visitStack.push_back(VisitStackElement(target, edgeBegin(target)));
          visitedNodes.insert(&target);
          visitor.visitEdge(*edge);
          visitor.visitNode(target);
          return run();
        }
      }
      // All children of this node have been visited or skipped, so remove
      // from the stack.
      visitStack.pop_back();
      visitor.popNode();
    }
  }

  Visitor &visitor;
  EdgePredicate edgePredicate;
  std::set<const NodeType *> visitedNodes;
  std::vector<VisitStackElement> visitStack;
};

} // namespace slang::netlist
