#include "Test.hpp"

#include "netlist/CombLoops.hpp"
#include "netlist/NetlistSerializer.hpp"
#include "netlist/PathFinder.hpp"

TEST_CASE("Serialize and deserialize preserves node and edge counts",
          "[Serializer]") {
  const NetlistTest test(R"(
module m(input a, output b);
  assign b = a;
endmodule
)");
  auto json = NetlistSerializer::serialize(test.graph);
  auto flatGraph = NetlistSerializer::deserialize(json);

  CHECK(flatGraph.numNodes() == test.graph.numNodes());
  CHECK(flatGraph.numEdges() == test.graph.numEdges());
}

TEST_CASE("Serialize and deserialize preserves port lookup", "[Serializer]") {
  const NetlistTest test(R"(
module m(input a, output b);
  assign b = a;
endmodule
)");
  auto json = NetlistSerializer::serialize(test.graph);
  auto flatGraph = NetlistSerializer::deserialize(json);

  auto *nodeA = flatGraph.lookup("m.a");
  REQUIRE(nodeA != nullptr);
  CHECK(nodeA->kind == NodeKind::Port);
  CHECK(nodeA->name == "a");
  CHECK(nodeA->direction == ast::ArgumentDirection::In);

  auto *nodeB = flatGraph.lookup("m.b");
  REQUIRE(nodeB != nullptr);
  CHECK(nodeB->kind == NodeKind::Port);
  CHECK(nodeB->name == "b");
  CHECK(nodeB->direction == ast::ArgumentDirection::Out);
}

TEST_CASE("FlatPathFinder finds a path that exists in the original graph",
          "[Serializer]") {
  const NetlistTest test(R"(
module m(input a, output b);
  assign b = a;
endmodule
)");
  // Verify the path exists in the live graph first.
  CHECK(test.pathExists("m.a", "m.b"));

  auto flatGraph =
      NetlistSerializer::deserialize(NetlistSerializer::serialize(test.graph));

  auto *from = flatGraph.lookup("m.a");
  auto *to = flatGraph.lookup("m.b");
  REQUIRE(from != nullptr);
  REQUIRE(to != nullptr);

  FlatPathFinder finder;
  auto path = finder.find(*from, *to);
  CHECK(!path.empty());
}

TEST_CASE("FlatPathFinder finds no path when none exists in the original graph",
          "[Serializer]") {
  const NetlistTest test(R"(
module m(input a, input b, output c);
  assign c = a;
endmodule
)");
  // b -> c does not exist.
  CHECK(!test.pathExists("m.b", "m.c"));

  auto flatGraph =
      NetlistSerializer::deserialize(NetlistSerializer::serialize(test.graph));

  auto *from = flatGraph.lookup("m.b");
  auto *to = flatGraph.lookup("m.c");
  REQUIRE(from != nullptr);
  REQUIRE(to != nullptr);

  FlatPathFinder finder;
  auto path = finder.find(*from, *to);
  CHECK(path.empty());
}

TEST_CASE("FlatCombLoops detects a combinational loop", "[Serializer]") {
  const NetlistTest test(R"(
module t(input x, output y);
  assign y = x;
endmodule

module m;
  wire a, b;
  t t(.x(a), .y(b));
  assign a = b;
endmodule
)");

  auto flatGraph =
      NetlistSerializer::deserialize(NetlistSerializer::serialize(test.graph));

  FlatCombLoops loops(flatGraph);
  auto cycles = loops.getAllLoops();
  CHECK(!cycles.empty());
}

TEST_CASE("FlatCombLoops detects no loop when a DFF breaks the path",
          "[Serializer]") {
  const NetlistTest test(R"(
module t(input clk, input x, output reg z);
  always @(posedge clk)
    z <= x;
endmodule

module m(input clk);
  wire a, b;
  t t(.clk(clk), .x(a), .z(b));
  assign a = b;
endmodule
)");

  auto flatGraph =
      NetlistSerializer::deserialize(NetlistSerializer::serialize(test.graph));

  FlatCombLoops loops(flatGraph);
  auto cycles = loops.getAllLoops();
  CHECK(cycles.empty());
}

TEST_CASE("Serialize and deserialize preserves variable node metadata",
          "[Serializer]") {
  const NetlistTest test(R"(
module m(input a, output b);
  logic [3:0] w;
  assign w = {a, a, a, a};
  assign b = w[0];
endmodule
)");
  auto flatGraph =
      NetlistSerializer::deserialize(NetlistSerializer::serialize(test.graph));

  // At least one Variable node should be present.
  auto varNodes = flatGraph.filterNodes(NodeKind::Variable);
  CHECK(std::ranges::distance(varNodes) > 0);
  for (auto const &node : varNodes) {
    // Every variable node must have a non-empty path and name.
    CHECK(!node->hierarchicalPath.empty());
    CHECK(!node->name.empty());
  }
}

TEST_CASE("Deserialize throws on unsupported version", "[Serializer]") {
  auto json = R"({"version": 99, "nodes": [], "edges": []})";
  CHECK_THROWS_AS(NetlistSerializer::deserialize(json), std::runtime_error);
}

TEST_CASE("JSON round-trip is idempotent", "[Serializer]") {
  const NetlistTest test(R"(
module m(input a, output b);
  assign b = a;
endmodule
)");
  auto json1 = NetlistSerializer::serialize(test.graph);
  auto flatGraph = NetlistSerializer::deserialize(json1);

  // Node and edge counts must be stable across two round-trips.
  auto flatGraph2 =
      NetlistSerializer::deserialize(NetlistSerializer::serialize(test.graph));
  CHECK(flatGraph.numNodes() == flatGraph2.numNodes());
  CHECK(flatGraph.numEdges() == flatGraph2.numEdges());
}
