#include "Test.hpp"
#include "netlist/CombLoops.hpp"
#include "netlist/NetlistSerializer.hpp"

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

/// Build a netlist from SV text, serialize it, deserialize into a fresh graph,
/// and return the new graph.
static auto roundTrip(NetlistTest const &test)
    -> std::unique_ptr<NetlistGraph> {
  auto json = NetlistSerializer::serialize(test.graph);
  auto loaded = std::make_unique<NetlistGraph>();
  NetlistSerializer::deserialize(json, *loaded);
  return loaded;
}

//===----------------------------------------------------------------------===//
// Tests
//===----------------------------------------------------------------------===//

TEST_CASE("Round-trip preserves node and edge counts", "[Serializer]") {
  auto const &tree = R"(
module m(input a, output b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);
  CHECK(loaded->numNodes() == test.graph.numNodes());
  CHECK(loaded->numEdges() == test.graph.numEdges());
}

TEST_CASE("Round-trip preserves FileTable", "[Serializer]") {
  auto const &tree = R"(
module m(input a, output b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);
  CHECK(loaded->fileTable.size() == test.graph.fileTable.size());
  for (size_t i = 0; i < loaded->fileTable.size(); ++i) {
    auto idx = static_cast<uint32_t>(i);
    CHECK(loaded->fileTable.getFilename(idx) ==
          test.graph.fileTable.getFilename(idx));
  }
}

TEST_CASE("Round-trip preserves TextLocation on nodes", "[Serializer]") {
  auto const &tree = R"(
module m(input a, output b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);

  // Check Port nodes have matching locations.
  for (auto const &nodePtr : test.graph.filterNodes(NodeKind::Port)) {
    auto const &orig = nodePtr->as<Port>();
    auto *found = loaded->lookup(orig.hierarchicalPath);
    REQUIRE(found != nullptr);
    auto const &port = found->as<Port>();
    CHECK(port.location.fileIndex == orig.location.fileIndex);
    CHECK(port.location.line == orig.location.line);
    CHECK(port.location.column == orig.location.column);
    // Transient SourceLocation should NOT survive round-trip.
    CHECK_FALSE(port.location.hasSourceLocation());
  }
}

TEST_CASE("Round-trip preserves TextLocation on edge symbols", "[Serializer]") {
  auto const &tree = R"(
module m(input a, output b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);

  // Find an edge with a non-empty symbol in the original graph.
  bool foundEdge = false;
  for (auto const &nodePtr : test.graph) {
    for (auto const &edgePtr : nodePtr->getOutEdges()) {
      if (edgePtr->symbol.empty()) {
        continue;
      }
      auto const &origSym = edgePtr->symbol;

      // Find the corresponding edge in the loaded graph.
      auto *srcNode = loaded->lookup(origSym.hierarchicalPath);
      if (!srcNode) {
        continue;
      }
      for (auto const &loadedEdge : srcNode->getOutEdges()) {
        if (loadedEdge->symbol.name == origSym.name) {
          CHECK(loadedEdge->symbol.location.fileIndex ==
                origSym.location.fileIndex);
          CHECK(loadedEdge->symbol.location.line == origSym.location.line);
          CHECK(loadedEdge->symbol.location.column == origSym.location.column);
          foundEdge = true;
        }
      }
    }
  }
  CHECK(foundEdge);
}

TEST_CASE("Path finding works on deserialised graph", "[Serializer]") {
  auto const &tree = R"(
module m(input a, output b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);

  auto *start = loaded->lookup("m.a");
  auto *end = loaded->lookup("m.b");
  REQUIRE(start != nullptr);
  REQUIRE(end != nullptr);
  PathFinder pathFinder;
  auto path = pathFinder.find(*start, *end);
  CHECK_FALSE(path.empty());
}

TEST_CASE("Comb loop detection works on deserialised graph", "[Serializer]") {
  auto const &tree = R"(
module t(input x, output y);
  assign y = x;
endmodule

module m;
  wire a, b;
  t t(.x(a), .y(b));
  assign a = b;
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);

  CombLoops combLoops(*loaded);
  auto cycles = combLoops.getAllLoops();
  CHECK(cycles.size() == 1);
}

TEST_CASE("Round-trip preserves edge attributes", "[Serializer]") {
  auto const &tree = R"(
module m(input clk, input a, output reg b);
  always @(posedge clk)
    b <= a;
endmodule
)";
  const NetlistTest test(tree);
  auto json = NetlistSerializer::serialize(test.graph);
  NetlistGraph loaded;
  NetlistSerializer::deserialize(json, loaded);

  // Collect edge kinds from both graphs.
  auto collectEdgeKinds = [](NetlistGraph const &g) {
    std::vector<ast::EdgeKind> kinds;
    for (auto const &node : g) {
      for (auto const &edge : node->getOutEdges()) {
        kinds.push_back(edge->edgeKind);
      }
    }
    return kinds;
  };

  auto origKinds = collectEdgeKinds(test.graph);
  auto loadedKinds = collectEdgeKinds(loaded);
  CHECK(origKinds == loadedKinds);
}

TEST_CASE("Empty graph round-trip", "[Serializer]") {
  NetlistGraph empty;
  auto json = NetlistSerializer::serialize(empty);
  NetlistGraph loaded;
  NetlistSerializer::deserialize(json, loaded);
  CHECK(loaded.numNodes() == 0);
  CHECK(loaded.numEdges() == 0);
  CHECK(loaded.fileTable.size() == 0);
}

TEST_CASE("Version mismatch throws error", "[Serializer]") {
  auto badJson =
      R"({"version": 99, "fileTable": [], "nodes": [], "edges": []})";
  NetlistGraph graph;
  CHECK_THROWS_AS(NetlistSerializer::deserialize(badJson, graph),
                  std::runtime_error);
}

TEST_CASE("Round-trip preserves port direction and bounds", "[Serializer]") {
  auto const &tree = R"(
module m(input [7:0] a, output [3:0] b);
  assign b = a[3:0];
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);

  auto *origA = test.graph.lookup("m.a");
  auto *loadedA = loaded->lookup("m.a");
  REQUIRE(origA != nullptr);
  REQUIRE(loadedA != nullptr);

  auto const &origPort = origA->as<Port>();
  auto const &loadedPort = loadedA->as<Port>();
  CHECK(loadedPort.direction == origPort.direction);
  CHECK(loadedPort.bounds.lower() == origPort.bounds.lower());
  CHECK(loadedPort.bounds.upper() == origPort.bounds.upper());
  CHECK(loadedPort.name == origPort.name);
}
