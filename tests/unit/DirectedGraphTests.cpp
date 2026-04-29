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

// addEdge after addNewEdge must return the *first* edge to the
// target, not allocate a third — verifying that addNewEdge seeds the
// outEdgeIndex when no entry exists yet.
TEST_CASE("addEdge after addNewEdge returns the first edge",
          "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &first = n0.addNewEdge(n1);
  auto &second = n0.addNewEdge(n1);
  auto &dedup = n0.addEdge(n1);
  CHECK(&dedup == &first);
  CHECK(&dedup != &second);
  CHECK(n0.outDegree() == 2); // The two parallel edges, no third.
  CHECK(n1.inDegree() == 2);
}

// addNewEdge after addEdge correctly produces a parallel edge while
// leaving the index pointing at the first edge.
TEST_CASE("addNewEdge after addEdge keeps index on the original",
          "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &original = n0.addEdge(n1);
  auto &parallel = n0.addNewEdge(n1);
  CHECK(&original != &parallel);
  CHECK(n0.outDegree() == 2);
  // Subsequent addEdge must return the original, not the parallel.
  auto &dedup = n0.addEdge(n1);
  CHECK(&dedup == &original);
  CHECK(n0.outDegree() == 2);
}

// removeEdge in the presence of parallel edges must re-point the
// index at the surviving edge, so the next addEdge dedupes against it
// instead of creating a new edge.
TEST_CASE("removeEdge re-points index when a parallel edge survives",
          "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  auto &first = n0.addEdge(n1);
  auto &parallel = n0.addNewEdge(n1);
  CHECK(graph.removeEdge(n0, n1));
  CHECK(n0.outDegree() == 1);
  // The first edge was removed (findEdgeTo returns the first match);
  // the parallel one should be the survivor.
  (void)first;
  auto &dedup = n0.addEdge(n1);
  CHECK(&dedup == &parallel);
  CHECK(n0.outDegree() == 1);
}

// removeEdge of the only edge to a target must drop the index entry
// so the next addEdge actually adds (rather than returning a stale
// pointer to the freed edge). We can't assert pointer inequality —
// the allocator may legitimately reuse the slot — but we can assert
// the graph is in the right shape and the new edge has live in/out
// bookkeeping on both sides.
TEST_CASE("removeEdge drops index entry when last edge to target removed",
          "[DirectedGraph]") {
  GraphType graph;
  auto &n0 = graph.addNode();
  auto &n1 = graph.addNode();
  n0.addEdge(n1);
  CHECK(graph.removeEdge(n0, n1));
  CHECK(n0.outDegree() == 0);
  CHECK(n1.inDegree() == 0);
  n0.addEdge(n1);
  CHECK(n0.outDegree() == 1);
  CHECK(n1.inDegree() == 1);
  // A second addEdge must dedup against the now-current entry.
  n0.addEdge(n1);
  CHECK(n0.outDegree() == 1);
}

// clearAllEdges must also reset the outEdgeIndex so subsequent
// addEdge calls don't dedup against a stale entry.
TEST_CASE("clearAllEdges resets the dedup index", "[DirectedGraph]") {
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
  // After clearing, addEdge must register the edge afresh — both the
  // out-edge list and the in-edge list on the target need it back.
  n0.addEdge(n1);
  CHECK(n0.outDegree() == 1);
  CHECK(n1.inDegree() == 1);
  // And dedup is back in working order.
  n0.addEdge(n1);
  CHECK(n0.outDegree() == 1);
}

// High fan-out smoke test: with the linear-scan dedup this would be
// O(N²) in addEdge; with the hash-indexed path it is O(N) total. Caps
// the design at a value that completes near-instantly with the index
// and would visibly stall (>1s) without it.
TEST_CASE("High fan-out addEdge stays linear", "[DirectedGraph]") {
  constexpr size_t kFanOut = 20'000;
  GraphType graph;
  auto &source = graph.addNode();
  std::vector<TestNode *> targets;
  targets.reserve(kFanOut);
  for (size_t i = 0; i < kFanOut; ++i) {
    targets.push_back(&graph.addNode());
  }
  for (auto *t : targets) {
    graph.addEdge(source, *t);
  }
  CHECK(source.outDegree() == kFanOut);
  // Repeating the same calls must dedup, not duplicate.
  for (auto *t : targets) {
    graph.addEdge(source, *t);
  }
  CHECK(source.outDegree() == kFanOut);
}
