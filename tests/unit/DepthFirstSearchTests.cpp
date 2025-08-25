#include "Test.hpp"
#include "netlist/DepthFirstSearch.hpp"
#include "netlist/DirectedGraph.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace slang::netlist;

namespace {

struct TestNode;
struct TestEdge;

size_t nodeIDs = 0;

struct TestNode : public Node<TestNode, TestEdge> {
  size_t ID;
  TestNode() : ID(nodeIDs++) {};
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
};

// A predicate to select edges that only connect nodes with even IDs.
struct EdgesToOnlyEvenNodes {
  EdgesToOnlyEvenNodes() = default;
  bool operator()(const TestEdge &edge) {
    return edge.getTargetNode().ID % 2 == 0;
  }
};

} // namespace

TEST_CASE("DFS visits all nodes in a ring, starting from each node") {
  for (size_t start = 0; start < 5; ++start) {
    DirectedGraph<TestNode, TestEdge> graph;
    std::vector<TestNode *> nodes;
    for (int i = 0; i < 5; ++i)
      nodes.push_back(&graph.addNode());
    for (int i = 0; i < 5; ++i)
      graph.addEdge(*nodes[i], *nodes[(i + 1) % 5]);
    TestVisitor visitor;
    DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor,
                                                          *nodes[start]);
    CHECK(visitor.nodes.size() == 5);
    CHECK(visitor.edges.size() == 4);
    // All nodes are unique
    std::set<TestNode *> uniqueNodes(visitor.nodes.begin(),
                                     visitor.nodes.end());
    CHECK(uniqueNodes.size() == 5);
  }
}

TEST_CASE("DFS visits all nodes in a tree, order is pre-order") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  auto &n3 = graph.addNode();
  auto &n4 = graph.addNode();
  auto &n5 = graph.addNode();
  auto &n6 = graph.addNode();
  graph.addEdge(n0, n1);
  graph.addEdge(n0, n2);
  graph.addEdge(n1, n3);
  graph.addEdge(n1, n4);
  graph.addEdge(n2, n5);
  graph.addEdge(n2, n6);
  TestVisitor visitor;
  DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 7);
  CHECK(visitor.edges.size() == 6);
  // All nodes are unique
  std::set<TestNode *> uniqueNodes(visitor.nodes.begin(), visitor.nodes.end());
  CHECK(uniqueNodes.size() == 7);
  // Root is first
  CHECK(*visitor.nodes[0] == n0);
}

TEST_CASE("DFS with edge predicate skips odd nodes") {
  nodeIDs = 0; // Reset node IDs for this test.
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  auto &n3 = graph.addNode();
  auto &n4 = graph.addNode();
  graph.addEdge(n0, n1);
  graph.addEdge(n0, n2);
  graph.addEdge(n0, n3);
  graph.addEdge(n0, n4);
  TestVisitor visitor;
  DepthFirstSearch<TestNode, TestEdge, TestVisitor, EdgesToOnlyEvenNodes> dfs(
      visitor, n0);
  CHECK(visitor.nodes.size() == 3);
  CHECK(visitor.edges.size() == 2);
  std::set<TestNode *> uniqueNodes(visitor.nodes.begin(), visitor.nodes.end());
  CHECK(uniqueNodes.count(&n0) == 1);
  CHECK(uniqueNodes.count(&n2) == 1);
  CHECK(uniqueNodes.count(&n4) == 1);
}

TEST_CASE("DFS on single node graph") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode();
  TestVisitor visitor;
  DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 1);
  CHECK(visitor.edges.empty());
  CHECK(*visitor.nodes[0] == n0);
}

TEST_CASE("DFS on disconnected graph only visits reachable nodes") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  auto &n3 = graph.addNode();
  // n0 -> n1, n2 is disconnected, n3 is disconnected
  graph.addEdge(n0, n1);
  TestVisitor visitor;
  DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 2);
  std::set<TestNode *> uniqueNodes(visitor.nodes.begin(), visitor.nodes.end());
  CHECK(uniqueNodes.count(&n0) == 1);
  CHECK(uniqueNodes.count(&n1) == 1);
  CHECK(uniqueNodes.count(&n2) == 0);
  CHECK(uniqueNodes.count(&n3) == 0);
}

TEST_CASE("DFS with cycles does not revisit nodes") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  graph.addEdge(n0, n1);
  graph.addEdge(n1, n2);
  graph.addEdge(n2, n0); // cycle
  TestVisitor visitor;
  DepthFirstSearch<TestNode, TestEdge, TestVisitor> dfs(visitor, n0);
  CHECK(visitor.nodes.size() == 3);
  std::set<TestNode *> uniqueNodes(visitor.nodes.begin(), visitor.nodes.end());
  CHECK(uniqueNodes.size() == 3);
}
