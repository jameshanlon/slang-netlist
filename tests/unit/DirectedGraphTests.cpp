#include "Test.hpp"
#include "netlist/DirectedGraph.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace slang::netlist;

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

TEST_CASE("Empty graph", "[DirectedGraph]") {
  const GraphType graph;
  CHECK(graph.numNodes() == 0);
  CHECK(graph.numEdges() == 0);
}

TEST_CASE("Self-loop edge", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  graph.addEdge(n0, n0);
  CHECK(graph.outDegree(n0) == 1);
  CHECK(graph.inDegree(n0) == 1);
}

TEST_CASE("Node equality and aliasing", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n0Alias = graph.getNode(graph.findNode(n0));
  CHECK(n0 == n0Alias);
  CHECK(n0 != n1);
}

TEST_CASE("Edge equality and uniqueness", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &e0a = n0.addEdge(n1);
  auto &e0b = n0.addEdge(n1);
  auto *e0c = n0.findEdgeTo(n1)->get();
  CHECK(e0a == e0b);
  CHECK(e0a == *e0c);
}

TEST_CASE("Edge inequality", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  auto &e0 = n0.addEdge(n1);
  auto &e1 = n1.addEdge(n2);
  CHECK(e0 != e1);
}

TEST_CASE("Basic connectivity and degrees", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  auto &n3 = graph.addNode();
  graph.addEdge(n0, n1);
  graph.addEdge(n0, n2);
  graph.addEdge(n0, n3);
  graph.addEdge(n1, n2);
  graph.addEdge(n1, n3);
  graph.addEdge(n2, n3);

  SECTION("Node and edge counts") {
    CHECK(graph.numNodes() == 4);
    CHECK(graph.numEdges() == 6);
  }
  SECTION("Out degrees") {
    CHECK(graph.outDegree(n0) == 3);
    CHECK(graph.outDegree(n1) == 2);
    CHECK(graph.outDegree(n2) == 1);
    CHECK(graph.outDegree(n3) == 0);
  }
  SECTION("In degrees") {
    CHECK(graph.inDegree(n0) == 0);
    CHECK(graph.inDegree(n1) == 1);
    CHECK(graph.inDegree(n2) == 2);
    CHECK(graph.inDegree(n3) == 3);
  }
}

TEST_CASE("Remove node updates degrees", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  graph.addEdge(n0, n1);
  graph.addEdge(n1, n0);
  CHECK(graph.removeNode(n0));
  CHECK(graph.findNode(n0) == GraphType::null_node);
  CHECK(n1.inDegree() == 0);
  CHECK(n1.outDegree() == 0);
}

TEST_CASE("Remove edge updates degrees", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  graph.addEdge(n0, n1);
  CHECK(graph.removeEdge(n0, n1));
  CHECK(graph.outDegree(n0) == 0);
  CHECK(graph.inDegree(n1) == 0);
  // Removing again should fail
  CHECK(!graph.removeEdge(n0, n1));
}

TEST_CASE("Iteration over nodes and edges", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  graph.addEdge(n0, n1);

  SECTION("Iterate nodes") {
    size_t count = 0;
    for (auto it = graph.begin(); it != graph.end(); ++it) {
      count++;
    }
    CHECK(count == graph.numNodes());
  }
  SECTION("Iterate edges") {
    size_t count = 0;
    for (auto it = n0.begin(); it != n0.end(); ++it) {
      count++;
    }
    CHECK(count == n0.outDegree());
  }
}

TEST_CASE("Clear all edges from node", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  n0.addEdge(n1);
  n0.addEdge(n2);
  n0.clearAllEdges();
  CHECK(n0.outDegree() == 0);
  CHECK(n1.inDegree() == 0);
  CHECK(n2.inDegree() == 0);
}

TEST_CASE("Remove non-existent node/edge", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto otherNode = TestNode{};
  CHECK(!graph.removeNode(otherNode)); // Not in graph
  CHECK(!graph.removeEdge(n0, n1));    // No edge exists
}

TEST_CASE("Duplicate edge is not added twice", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &e1 = graph.addEdge(n0, n1);
  auto &e2 = graph.addEdge(n0, n1); // Should not add a new edge
  CHECK(&e1 == &e2);
  CHECK(graph.numEdges() == 1);
  CHECK(n0.outDegree() == 1);
  CHECK(n1.inDegree() == 1);
}

TEST_CASE("Remove edge from node with multiple edges", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  graph.addEdge(n0, n1);
  graph.addEdge(n0, n2);
  CHECK(graph.removeEdge(n0, n1));
  CHECK(n0.outDegree() == 1);
  CHECK(n1.inDegree() == 0);
  CHECK(n2.inDegree() == 1);
}

TEST_CASE("Remove all nodes from graph", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  graph.addEdge(n0, n1);
  graph.addEdge(n1, n2);
  CHECK(graph.removeNode(n0));
  CHECK(graph.removeNode(n1));
  CHECK(graph.removeNode(n2));
  CHECK(graph.numNodes() == 0);
  CHECK(graph.numEdges() == 0);
}

TEST_CASE("Get edges to a node", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &n2 = graph.addNode();
  graph.addEdge(n0, n2);
  graph.addEdge(n1, n2);
  std::vector<TestEdge *> result;
  CHECK(n0.getEdgesTo(n2, result));
  CHECK(result.size() == 1);
  result.clear();
  CHECK(n1.getEdgesTo(n2, result));
  CHECK(result.size() == 1);
  result.clear();
  CHECK_FALSE(n2.getEdgesTo(n0, result));
  CHECK(result.empty());
}

TEST_CASE("Graph with no edges", "[DirectedGraph]") {
  GraphType graph;
  for (int i = 0; i < 5; ++i) {
    graph.addNode();
  }
  for (size_t i = 0; i < graph.numNodes(); ++i) {
    CHECK(graph.getNode(i).inDegree() == 0);
    CHECK(graph.getNode(i).outDegree() == 0);
  }
  CHECK(graph.numEdges() == 0);
}

TEST_CASE("Self-loop removal", "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  graph.addEdge(n0, n0);
  CHECK(graph.removeEdge(n0, n0));
  CHECK(n0.inDegree() == 0);
  CHECK(n0.outDegree() == 0);
}
