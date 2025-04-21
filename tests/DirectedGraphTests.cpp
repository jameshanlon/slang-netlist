#include <catch2/catch_test_macros.hpp>

#include "crumple/utility/DirectedGraph.hpp"

struct TestNode;
struct TestEdge;

using GraphType = DirectedGraph<TestNode, TestEdge>;

struct TestNode : public Node<TestNode, TestEdge> {
  TestNode() = default;
};

struct TestEdge : public DirectedEdge<TestNode, TestEdge> {
  TestEdge(TestNode &sourceNode, TestNode &targetNode)
      : DirectedEdge(sourceNode, targetNode) {}
};

TEST_CASE("Test node and edge equality") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode(std::make_unique<TestNode>());
  auto &n1 = graph.addNode(std::make_unique<TestNode>());
  auto &n2 = graph.addNode();
  auto &n3 = graph.addNode();
  auto &n0Alias = graph.getNode(graph.findNode(n0));
  CHECK(n0 == n0Alias);
  CHECK(n0 != n1);
  auto &e0a = n0.addEdge(n1);
  auto &e0b = n0.addEdge(n1);
  auto *e0c = n0.findEdgeTo(n1)->get();
  CHECK(e0a == e0b);
  CHECK(e0a == *e0c);
  auto &e1 = n1.addEdge(n2);
  auto &e2 = n2.addEdge(n3);
  CHECK(e0a != e1);
  CHECK(e0b != e1);
  CHECK(*e0c != e1);
  CHECK(e1 != e2);
}

TEST_CASE("Test basic connectivity") {
  DirectedGraph<TestNode, TestEdge> graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  auto &n3 = graph.addNode();
  CHECK(graph.numNodes() == 4);
  CHECK(graph.numEdges() == 0);
  auto &e0 = graph.addEdge(n0, n1);
  auto &e1 = graph.addEdge(n0, n2);
  auto &e2 = graph.addEdge(n0, n3);
  auto &e3 = graph.addEdge(n1, n2);
  auto &e4 = graph.addEdge(n1, n3);
  auto &e5 = graph.addEdge(n2, n3);
  CHECK(graph.numEdges() == 6);
  // Edge target nodes.
  CHECK(e0.getTargetNode() == n1);
  CHECK(e1.getTargetNode() == n2);
  CHECK(e2.getTargetNode() == n3);
  CHECK(e3.getTargetNode() == n2);
  CHECK(e4.getTargetNode() == n3);
  CHECK(e5.getTargetNode() == n3);
  // Edge source nodes.
  CHECK(e0.getSourceNode() == n0);
  CHECK(e1.getSourceNode() == n0);
  CHECK(e2.getSourceNode() == n0);
  CHECK(e3.getSourceNode() == n1);
  CHECK(e4.getSourceNode() == n1);
  CHECK(e5.getSourceNode() == n2);
  // Out degrees.
  CHECK(graph.outDegree(n0) == 3);
  CHECK(graph.outDegree(n1) == 2);
  CHECK(graph.outDegree(n2) == 1);
  CHECK(graph.outDegree(n3) == 0);
  CHECK(n0.outDegree() == 3);
  CHECK(n1.outDegree() == 2);
  CHECK(n2.outDegree() == 1);
  CHECK(n3.outDegree() == 0);
  // In degrees.
  CHECK(graph.inDegree(n0) == 0);
  CHECK(graph.inDegree(n1) == 1);
  CHECK(graph.inDegree(n2) == 2);
  CHECK(graph.inDegree(n3) == 3);
}

#define TEST_GRAPH \
  DirectedGraph<TestNode, TestEdge> graph; \
  auto &n0 = graph.addNode(); \
  auto &n1 = graph.addNode(); \
  auto &n2 = graph.addNode(); \
  auto &n3 = graph.addNode(); \
  auto &n4 = graph.addNode(); \
  /* n0 connects to n1, n2, n3, n4. */ \
  graph.addEdge(n0, n1); \
  graph.addEdge(n0, n2); \
  graph.addEdge(n0, n3); \
  graph.addEdge(n0, n4); \
  /* n1, n2, n3, n4 connect back to n0. */ \
  graph.addEdge(n1, n0); \
  graph.addEdge(n2, n0); \
  graph.addEdge(n3, n0); \
  graph.addEdge(n4, n0); \
  /* n1, n2, n3, n4 connected in a ring. */ \
  graph.addEdge(n1, n2); \
  graph.addEdge(n2, n3); \
  graph.addEdge(n3, n4); \
  graph.addEdge(n4, n1)

TEST_CASE("Test graph") {
  TEST_GRAPH;
  CHECK(graph.numNodes() == 5);
  CHECK(graph.numEdges() == 12);
  CHECK(n0.inDegree() == 4);
  CHECK(n0.outDegree() == 4);
  CHECK(n1.inDegree() == 2);
  CHECK(n1.outDegree() == 2);
  CHECK(n2.inDegree() == 2);
  CHECK(n2.outDegree() == 2);
  CHECK(n3.inDegree() == 2);
  CHECK(n3.outDegree() == 2);
  CHECK(n4.inDegree() == 2);
  CHECK(n4.outDegree() == 2);
}

TEST_CASE("Test removing nodes") {
  TEST_GRAPH;
  // Remove n0.
  CHECK(graph.removeNode(n0));
  CHECK(graph.findNode(n0) == GraphType::null_node);
  CHECK(n1.inDegree() == 1);
  CHECK(n1.outDegree() == 1);
  CHECK(n2.inDegree() == 1);
  CHECK(n2.outDegree() == 1);
  CHECK(n3.inDegree() == 1);
  CHECK(n3.outDegree() == 1);
  CHECK(n4.inDegree() == 1);
  CHECK(n4.outDegree() == 1);
  // Remove n1.
  CHECK(graph.removeNode(n1));
  CHECK(graph.findNode(n1) == GraphType::null_node);
  CHECK(n2.inDegree() == 0);
  CHECK(n2.outDegree() == 1);
  CHECK(n3.inDegree() == 1);
  CHECK(n3.outDegree() == 1);
  CHECK(n4.inDegree() == 1);
  CHECK(n4.outDegree() == 0);
  // Remove n2.
  CHECK(graph.removeNode(n2));
  CHECK(graph.findNode(n2) == GraphType::null_node);
  CHECK(n3.inDegree() == 0);
  CHECK(n3.outDegree() == 1);
  CHECK(n4.inDegree() == 1);
  CHECK(n4.outDegree() == 0);
}

TEST_CASE("Test removing edges") {
  TEST_GRAPH;
  // Remove edge n0 -> n1.
  CHECK(graph.removeEdge(n0, n1));
  CHECK(graph.outDegree(n0) == 3);
  CHECK(graph.inDegree(n1) == 1);
  // Remove n1 -> n2.
  CHECK(graph.removeEdge(n1, n2));
  CHECK(graph.outDegree(n1) == 1);
  CHECK(graph.inDegree(n2) == 1);
  // Remove n2 -> n3.
  CHECK(graph.removeEdge(n2, n3));
  CHECK(graph.outDegree(n2) == 1);
  CHECK(graph.inDegree(n3) == 1);
  // Edges no longer exist.
  CHECK(!graph.removeEdge(n0, n1));
  CHECK(!graph.removeEdge(n1, n2));
  CHECK(!graph.removeEdge(n2, n3));
}

TEST_CASE("Test clearing all edges from a node") {
  TEST_GRAPH;
  n0.clearAllEdges();
  CHECK(n1.inDegree() == 1);
  CHECK(n1.outDegree() == 1);
  CHECK(n2.inDegree() == 1);
  CHECK(n2.outDegree() == 1);
  CHECK(n3.inDegree() == 1);
  CHECK(n3.outDegree() == 1);
  CHECK(n4.inDegree() == 1);
  CHECK(n4.outDegree() == 1);
}

TEST_CASE("Test iterating over nodes and their edges") {
  TEST_GRAPH;
  // Nodes in the graph.
  {
    size_t count = 0;
    for (auto it = graph.begin(); it != graph.end(); it++) {
      count++;
    }
    CHECK(count == graph.numNodes());
  }
  // n0 outgoing edges.
  {
    auto &node = graph.getNode(0);
    size_t count = 0;
    for (auto it = node.begin(); it != node.end(); it++) {
      count++;
    }
    CHECK(count == node.outDegree());
  }
  // n0 incoming edges.
  {
    auto &node = graph.getNode(0);
    size_t count = 0;
    for (auto it = node.getInEdges().begin(); it != node.getInEdges().end();
         it++) {
      count++;
    }
    CHECK(count == node.inDegree());
  }
  // n3 outgoing edges.
  {
    auto &node = graph.getNode(3);
    size_t count = 0;
    for (auto it = node.begin(); it != node.end(); it++) {
      count++;
    }
    CHECK(count == node.outDegree());
  }
  // n3 incoming edges.
  {
    auto &node = graph.getNode(3);
    size_t count = 0;
    for (auto it = node.getInEdges().begin(); it != node.getInEdges().end();
         it++) {
      count++;
    }
    CHECK(count == node.inDegree());
  }
}
