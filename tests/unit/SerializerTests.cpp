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
    CHECK(std::string(loaded->fileTable.getFilename(idx)) ==
          std::string(test.graph.fileTable.getFilename(idx)));
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

TEST_CASE("Round-trip preserves Conditional and Merge nodes", "[Serializer]") {
  auto const &tree = R"(
module m(input logic a, input logic c, output logic b);
  always_comb begin
    if (c)
      b = a;
    else
      b = 1'b0;
  end
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);
  CHECK(loaded->numNodes() == test.graph.numNodes());
  CHECK(loaded->numEdges() == test.graph.numEdges());

  // Verify Conditional and Merge node kinds survived.
  CHECK_FALSE(loaded->filterNodes(NodeKind::Conditional).empty());
  CHECK_FALSE(loaded->filterNodes(NodeKind::Merge).empty());
}

TEST_CASE("Round-trip preserves Case nodes", "[Serializer]") {
  auto const &tree = R"(
module m(input logic [1:0] sel, input logic a, output logic b);
  always_comb begin
    case (sel)
      0: b = a;
      1: b = 1'b0;
      default: b = 1'b1;
    endcase
  end
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);
  CHECK(loaded->numNodes() == test.graph.numNodes());
  CHECK(loaded->numEdges() == test.graph.numEdges());
  CHECK_FALSE(loaded->filterNodes(NodeKind::Case).empty());
}

TEST_CASE("Round-trip preserves State nodes", "[Serializer]") {
  auto const &tree = R"(
module m(input logic clk, input logic d, output logic q);
  logic r;
  always_ff @(posedge clk)
    r <= d;
  assign q = r;
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);
  CHECK(loaded->numNodes() == test.graph.numNodes());
  CHECK(loaded->numEdges() == test.graph.numEdges());

  auto stateNodes = loaded->filterNodes(NodeKind::State);
  CHECK_FALSE(stateNodes.empty());
  auto const &state = stateNodes.front()->as<State>();
  CHECK(state.name == "r");
}

TEST_CASE("Round-trip preserves Variable nodes (interface)", "[Serializer]") {
  auto const &tree = R"(
interface ifc;
  logic val;
  modport producer(output val);
  modport consumer(input val);
endinterface

module producer(ifc.producer p);
  assign p.val = 1'b1;
endmodule

module consumer(ifc.consumer p);
endmodule

module top;
  ifc i();
  producer prod(.p(i.producer));
  consumer cons(.p(i.consumer));
endmodule
)";
  const NetlistTest test(tree);

  auto varNodes = test.graph.filterNodes(NodeKind::Variable);
  if (!varNodes.empty()) {
    auto loaded = roundTrip(test);
    CHECK(loaded->numNodes() == test.graph.numNodes());
    CHECK_FALSE(loaded->filterNodes(NodeKind::Variable).empty());
  }
}

TEST_CASE("Round-trip preserves disabled edges", "[Serializer]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);

  // Disable an edge.
  for (auto &node : test.graph) {
    for (auto &edge : node->getOutEdges()) {
      edge->disable();
      break;
    }
    break;
  }

  auto loaded = roundTrip(test);

  // Count disabled edges in both graphs.
  auto countDisabled = [](NetlistGraph const &g) {
    size_t count = 0;
    for (auto const &node : g) {
      for (auto const &edge : node->getOutEdges()) {
        if (edge->disabled)
          count++;
      }
    }
    return count;
  };
  CHECK(countDisabled(*loaded) == countDisabled(test.graph));
  CHECK(countDisabled(*loaded) > 0);
}

TEST_CASE("Round-trip preserves NegEdge kind", "[Serializer]") {
  auto const &tree = R"(
module m(input clk, input a, output reg b);
  always @(negedge clk)
    b <= a;
endmodule
)";
  const NetlistTest test(tree);

  // Verify there's a NegEdge in the original.
  bool hasNegEdge = false;
  for (auto const &node : test.graph) {
    for (auto const &edge : node->getOutEdges()) {
      if (edge->edgeKind == ast::EdgeKind::NegEdge)
        hasNegEdge = true;
    }
  }
  CHECK(hasNegEdge);

  auto loaded = roundTrip(test);
  bool loadedHasNegEdge = false;
  for (auto const &node : *loaded) {
    for (auto const &edge : node->getOutEdges()) {
      if (edge->edgeKind == ast::EdgeKind::NegEdge)
        loadedHasNegEdge = true;
    }
  }
  CHECK(loadedHasNegEdge);
}

TEST_CASE("Round-trip preserves InOut port direction", "[Serializer]") {
  auto const &tree = R"(
module m(inout wire a);
endmodule
)";
  const NetlistTest test(tree);
  auto loaded = roundTrip(test);

  auto portNodes = loaded->filterNodes(NodeKind::Port);
  bool foundInOut = false;
  for (auto const &node : portNodes) {
    auto const &port = node->as<Port>();
    if (port.direction == ast::ArgumentDirection::InOut)
      foundInOut = true;
  }
  CHECK(foundInOut);
}

TEST_CASE("Malformed JSON throws error", "[Serializer]") {
  NetlistGraph graph;
  CHECK_THROWS_AS(NetlistSerializer::deserialize("not valid json", graph),
                  std::runtime_error);
}
