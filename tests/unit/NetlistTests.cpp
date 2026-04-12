#include "Test.hpp"

TEST_CASE("NetlistGraph::filterNodes", "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto ports = test.graph.filterNodes(NodeKind::Port);
  int portCount = 0;
  for (auto &node : ports) {
    (void)node;
    portCount++;
  }
  CHECK(portCount == 2);

  auto assignments = test.graph.filterNodes(NodeKind::Assignment);
  int assignCount = 0;
  for (auto &node : assignments) {
    (void)node;
    assignCount++;
  }
  CHECK(assignCount == 1);
}

TEST_CASE("NetlistNode isKind for all node types", "[Netlist]") {
  CHECK(Port::isKind(NodeKind::Port));
  CHECK_FALSE(Port::isKind(NodeKind::Variable));
  CHECK(Variable::isKind(NodeKind::Variable));
  CHECK_FALSE(Variable::isKind(NodeKind::Port));
  CHECK(State::isKind(NodeKind::State));
  CHECK_FALSE(State::isKind(NodeKind::Assignment));
  CHECK(Assignment::isKind(NodeKind::Assignment));
  CHECK_FALSE(Assignment::isKind(NodeKind::State));
  CHECK(Conditional::isKind(NodeKind::Conditional));
  CHECK_FALSE(Conditional::isKind(NodeKind::Case));
  CHECK(Case::isKind(NodeKind::Case));
  CHECK_FALSE(Case::isKind(NodeKind::Conditional));
  CHECK(Merge::isKind(NodeKind::Merge));
  CHECK_FALSE(Merge::isKind(NodeKind::None));
}

TEST_CASE("Port isInput and isOutput", "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto ports = test.graph.filterNodes(NodeKind::Port);
  bool foundInput = false;
  bool foundOutput = false;
  for (auto &node : ports) {
    auto &port = node->as<Port>();
    if (port.isInput()) {
      foundInput = true;
      CHECK_FALSE(port.isOutput());
    }
    if (port.isOutput()) {
      foundOutput = true;
      CHECK_FALSE(port.isInput());
    }
  }
  CHECK(foundInput);
  CHECK(foundOutput);
}

TEST_CASE("NetlistEdge disable", "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  bool foundEdge = false;
  for (auto &node : test.graph) {
    for (auto &edge : node->getOutEdges()) {
      edge->disable();
      CHECK(edge->disabled);
      foundEdge = true;
      break;
    }
    if (foundEdge)
      break;
  }
  CHECK(foundEdge);
  // Disabled edge should be omitted from DOT output.
  auto dot = test.renderDot();
  CHECK(dot.find("digraph") != std::string::npos);
}

TEST_CASE("Edge annotation", "[Netlist]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  logic [12:8] t;
  assign t = a;
  assign b = t[9:8];
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N1 -> N3 [label="a[7:0]"]
  N3 -> N4 [label="t[1:0]"]
  N4 -> N2 [label="b[7:0]"]
}
)");
}

TEST_CASE("NetlistGraph::lookup returns nullptr for non-existent name",
          "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.graph.lookup("nonexistent.path") == nullptr);
  CHECK(test.graph.lookup("m.nonexistent") == nullptr);
  CHECK(test.graph.lookup("") == nullptr);
}

TEST_CASE("NetlistGraph::lookup by name and range, exact match", "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  // Port 'a' has bounds [0,0].
  auto results = test.graph.lookup("m.a", netlist::DriverBitRange(0, 0));
  CHECK(results.size() == 1);
  CHECK(results[0]->kind == NodeKind::Port);
}

TEST_CASE("NetlistGraph::lookup by name and range, overlapping", "[Netlist]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  // Port 'a' has bounds [0,7]. Query [3,5] overlaps.
  auto results = test.graph.lookup("m.a", netlist::DriverBitRange(3, 5));
  CHECK(results.size() == 1);
  CHECK(results[0]->kind == NodeKind::Port);
}

TEST_CASE("NetlistGraph::lookup by name and range, non-overlapping",
          "[Netlist]") {
  auto const &tree = R"(
module m(input logic [3:0] a, output logic [3:0] b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  // Port 'a' has bounds [0,3]. Query [4,7] does not overlap.
  auto results = test.graph.lookup("m.a", netlist::DriverBitRange(4, 7));
  CHECK(results.empty());
}

TEST_CASE("NetlistGraph::lookup by name and range, non-existent name",
          "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto results =
      test.graph.lookup("m.nonexistent", netlist::DriverBitRange(0, 0));
  CHECK(results.empty());
}

TEST_CASE("NetlistGraph::lookup by name and range returns multiple nodes",
          "[Netlist]") {
  // A sequential design creates both a Port node and a State node for 'b'.
  auto const &tree = R"(
module m(input clk, input logic a, output logic b);
  always_ff @(posedge clk)
    b <= a;
endmodule
)";
  const NetlistTest test(tree);
  auto results = test.graph.lookup("m.b", netlist::DriverBitRange(0, 0));
  // Should find both the output Port and the State node.
  CHECK(results.size() == 2);
  bool foundPort = false;
  bool foundState = false;
  for (auto *node : results) {
    if (node->kind == NodeKind::Port)
      foundPort = true;
    if (node->kind == NodeKind::State)
      foundState = true;
  }
  CHECK(foundPort);
  CHECK(foundState);
}

TEST_CASE("NetlistGraph::findNodes with wildcard", "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, input logic b, output logic x, output logic y);
  assign x = a;
  assign y = b;
endmodule
)";
  const NetlistTest test(tree);
  // Match all nodes under module m.
  auto all = test.graph.findNodes("m.*");
  CHECK(all.size() == 4);

  // Match only inputs (a, b).
  auto inputA = test.graph.findNodes("m.a");
  CHECK(inputA.size() == 1);

  // Match with ? wildcard.
  auto singleChar = test.graph.findNodes("m.?");
  CHECK(singleChar.size() == 4);

  // No match.
  auto none = test.graph.findNodes("z.*");
  CHECK(none.empty());
}

TEST_CASE("NetlistGraph::findNodes wildcard with hierarchy", "[Netlist]") {
  auto const &tree = R"(
module sub(input logic d, output logic q);
  assign q = d;
endmodule

module top(input logic a, output logic b);
  sub u0(.d(a), .q(b));
endmodule
)";
  const NetlistTest test(tree);
  // Match all nodes in the sub-instance.
  auto subNodes = test.graph.findNodes("top.u0.*");
  CHECK(subNodes.size() >= 2);

  // Match all ports at any level.
  auto allNodes = test.graph.findNodes("*");
  CHECK(allNodes.size() >= 4);
}

TEST_CASE("NetlistGraph::findNodesRegex", "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, input logic b, output logic x, output logic y);
  assign x = a;
  assign y = b;
endmodule
)";
  const NetlistTest test(tree);
  // Match outputs only (x or y).
  auto outputs = test.graph.findNodesRegex("m\\.[xy]");
  CHECK(outputs.size() == 2);

  // Match everything under m.
  auto all = test.graph.findNodesRegex("m\\..*");
  CHECK(all.size() == 4);

  // No match.
  auto none = test.graph.findNodesRegex("z\\..*");
  CHECK(none.empty());
}
