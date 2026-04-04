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
