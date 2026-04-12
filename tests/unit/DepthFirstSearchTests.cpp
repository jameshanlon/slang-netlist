#include "Test.hpp"
#include "netlist/DepthFirstSearch.hpp"
#include "netlist/DirectedGraph.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace slang::netlist;

namespace {

struct TestNode;
struct TestEdge;

struct TestNode : public Node<TestNode, TestEdge> {
  size_t ID;
  TestNode(size_t ID) : ID(ID) {};
};

struct TestEdge : public DirectedEdge<TestNode, TestEdge> {
  TestEdge(TestNode &sourceNode, TestNode &targetNode)
      : DirectedEdge(sourceNode, targetNode) {}
};

struct TestVisitor {
  std::vector<TestNode *> nodes;
  std::vector<TestEdge *> edges;
  TestVisitor() = default;
  void visitedNode(TestNode &node) {};
  void visitNode(TestNode &node) { nodes.push_back(&node); };
  void visitEdge(TestEdge &edge) { edges.push_back(&edge); };
  void popNode() {}
};

// A predicate to select edges that only connect nodes with even IDs.
struct EdgesToOnlyEvenNodes {
  EdgesToOnlyEvenNodes() = default;
  auto operator()(const TestEdge &edge) -> bool {
    return edge.getTargetNode().ID % 2 == 0;
  }
};

} // namespace

TEST_CASE("DFS visits all nodes in a ring, starting from each node",
          "[DepthFirstSearch]") {
  for (size_t start = 0; start < 5; ++start) {
    DirectedGraph<TestNode, TestEdge> graph;
    std::vector<TestNode *> nodes;
    nodes.reserve(5);
    for (size_t i = 0; i < 5; ++i) {
      nodes.push_back(&graph.addNode(std::make_unique<TestNode>(i)));
    }
    for (int i = 0; i < 5; ++i) {
      graph.addEdge(*nodes[i], *nodes[(i + 1) % 5]);
    }
    TestVisitor visitor;
    const DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor,
                                                                *nodes[start]);
    CHECK(visitor.nodes.size() == 5);
    CHECK(visitor.edges.size() == 4);
    // All nodes are unique
    const std::set<TestNode *> uniqueNodes(visitor.nodes.begin(),
                                           visitor.nodes.end());
    CHECK(uniqueNodes.size() == 5);
  }
}

TEST_CASE("DFS visits all nodes in a tree, order is pre-order",
          "[DepthFirstSearch]") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>(0));
  auto &n1 = graph.addNode(std::make_unique<TestNode>(1));
  auto &n2 = graph.addNode(std::make_unique<TestNode>(2));
  auto &n3 = graph.addNode(std::make_unique<TestNode>(3));
  auto &n4 = graph.addNode(std::make_unique<TestNode>(4));
  auto &n5 = graph.addNode(std::make_unique<TestNode>(5));
  auto &n6 = graph.addNode(std::make_unique<TestNode>(6));
  graph.addEdge(n0, n1);
  graph.addEdge(n0, n2);
  graph.addEdge(n1, n3);
  graph.addEdge(n1, n4);
  graph.addEdge(n2, n5);
  graph.addEdge(n2, n6);
  TestVisitor visitor;
  const DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 7);
  CHECK(visitor.edges.size() == 6);
  // All nodes are unique
  const std::set<TestNode *> uniqueNodes(visitor.nodes.begin(),
                                         visitor.nodes.end());
  CHECK(uniqueNodes.size() == 7);
  // Root is first
  CHECK(*visitor.nodes[0] == n0);
}

TEST_CASE("DFS with edge predicate skips odd nodes", "[DepthFirstSearch]") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>(0));
  auto &n1 = graph.addNode(std::make_unique<TestNode>(1));
  auto &n2 = graph.addNode(std::make_unique<TestNode>(2));
  auto &n3 = graph.addNode(std::make_unique<TestNode>(3));
  auto &n4 = graph.addNode(std::make_unique<TestNode>(4));
  graph.addEdge(n0, n1);
  graph.addEdge(n0, n2);
  graph.addEdge(n0, n3);
  graph.addEdge(n0, n4);
  TestVisitor visitor;
  const DepthFirstSearch<TestNode, TestEdge, TestVisitor, EdgesToOnlyEvenNodes>
      dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 3);
  CHECK(visitor.edges.size() == 2);
  const std::set<TestNode *> uniqueNodes(visitor.nodes.begin(),
                                         visitor.nodes.end());
  CHECK(uniqueNodes.contains(&n0));
  CHECK(uniqueNodes.contains(&n2));
  CHECK(uniqueNodes.contains(&n4));
}

TEST_CASE("DFS on single node graph", "[DepthFirstSearch]") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>(0));
  TestVisitor visitor;
  const DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 1);
  CHECK(visitor.edges.empty());
  CHECK(*visitor.nodes[0] == n0);
}

TEST_CASE("DFS on disconnected graph only visits reachable nodes",
          "[DepthFirstSearch]") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>(0));
  auto &n1 = graph.addNode(std::make_unique<TestNode>(1));
  auto &n2 = graph.addNode(std::make_unique<TestNode>(2));
  auto &n3 = graph.addNode(std::make_unique<TestNode>(3));
  // n0 -> n1, n2 is disconnected, n3 is disconnected
  graph.addEdge(n0, n1);
  TestVisitor visitor;
  const DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 2);
  const std::set<TestNode *> uniqueNodes(visitor.nodes.begin(),
                                         visitor.nodes.end());
  CHECK(uniqueNodes.contains(&n0));
  CHECK(uniqueNodes.contains(&n1));
  CHECK(!uniqueNodes.contains(&n2));
  CHECK(!uniqueNodes.contains(&n3));
}

TEST_CASE("DFS with cycles does not revisit nodes", "[DepthFirstSearch]") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>(0));
  auto &n1 = graph.addNode(std::make_unique<TestNode>(1));
  auto &n2 = graph.addNode(std::make_unique<TestNode>(2));
  graph.addEdge(n0, n1);
  graph.addEdge(n1, n2);
  graph.addEdge(n2, n0); // cycle
  TestVisitor visitor;
  const DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 3);
  const std::set<TestNode *> uniqueNodes(visitor.nodes.begin(),
                                         visitor.nodes.end());
  CHECK(uniqueNodes.size() == 3);
}

TEST_CASE("Backward DFS visits predecessors in a chain", "[DepthFirstSearch]") {
  // n0 -> n1 -> n2 -> n3; backward from n3 should visit all.
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>(0));
  auto &n1 = graph.addNode(std::make_unique<TestNode>(1));
  auto &n2 = graph.addNode(std::make_unique<TestNode>(2));
  auto &n3 = graph.addNode(std::make_unique<TestNode>(3));
  graph.addEdge(n0, n1);
  graph.addEdge(n1, n2);
  graph.addEdge(n2, n3);
  TestVisitor visitor;
  DepthFirstSearch<TestNode, TestEdge, TestVisitor, select_all,
                   Direction::Backward>
      dfs(visitor, n3);
  CHECK(visitor.nodes.size() == 4);
  const std::set<TestNode *> visited(visitor.nodes.begin(),
                                     visitor.nodes.end());
  CHECK(visited.contains(&n0));
  CHECK(visited.contains(&n1));
  CHECK(visited.contains(&n2));
  CHECK(visited.contains(&n3));
}

TEST_CASE("Backward DFS only visits reachable predecessors",
          "[DepthFirstSearch]") {
  // n0 -> n1, n2 -> n1; backward from n1 should visit n0, n1, n2.
  // n3 is disconnected and should not be visited.
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>(0));
  auto &n1 = graph.addNode(std::make_unique<TestNode>(1));
  auto &n2 = graph.addNode(std::make_unique<TestNode>(2));
  auto &n3 = graph.addNode(std::make_unique<TestNode>(3));
  graph.addEdge(n0, n1);
  graph.addEdge(n2, n1);
  TestVisitor visitor;
  DepthFirstSearch<TestNode, TestEdge, TestVisitor, select_all,
                   Direction::Backward>
      dfs(visitor, n1);
  CHECK(visitor.nodes.size() == 3);
  const std::set<TestNode *> visited(visitor.nodes.begin(),
                                     visitor.nodes.end());
  CHECK(visited.contains(&n0));
  CHECK(visited.contains(&n1));
  CHECK(visited.contains(&n2));
  CHECK_FALSE(visited.contains(&n3));
}

TEST_CASE("Backward DFS with edge predicate", "[DepthFirstSearch]") {
  // n0(even) -> n1(odd) -> n2(even); backward from n2 with even-only
  // predicate should stop at n1 (odd), visiting only n2.
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>(0));
  auto &n1 = graph.addNode(std::make_unique<TestNode>(1));
  auto &n2 = graph.addNode(std::make_unique<TestNode>(2));
  graph.addEdge(n0, n1);
  graph.addEdge(n1, n2);

  struct EdgesFromEvenNodes {
    auto operator()(const TestEdge &edge) -> bool {
      return edge.getSourceNode().ID % 2 == 0;
    }
  };

  TestVisitor visitor;
  DepthFirstSearch<TestNode, TestEdge, TestVisitor, EdgesFromEvenNodes,
                   Direction::Backward>
      dfs(visitor, EdgesFromEvenNodes{}, n2);
  CHECK(visitor.nodes.size() == 1);
  CHECK(*visitor.nodes[0] == n2);
}
