#include "Test.hpp"

TEST_CASE("DOT output for simple continuous assignment", "[Dot]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto dot = test.renderDot();
  CHECK(dot.find("digraph") != std::string::npos);
  CHECK(dot.find("In port a") != std::string::npos);
  CHECK(dot.find("Out port b") != std::string::npos);
  CHECK(dot.find("Assignment") != std::string::npos);
  CHECK(dot.find("a[") != std::string::npos);
  CHECK(dot.find("b[") != std::string::npos);
}

TEST_CASE("DOT output for conditional (if/else)", "[Dot]") {
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
  auto dot = test.renderDot();
  CHECK(dot.find("Conditional") != std::string::npos);
  CHECK(dot.find("Merge") != std::string::npos);
  CHECK(dot.find("In port a") != std::string::npos);
  CHECK(dot.find("In port c") != std::string::npos);
  CHECK(dot.find("Out port b") != std::string::npos);
}

TEST_CASE("DOT output for case statement", "[Dot]") {
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
  auto dot = test.renderDot();
  CHECK(dot.find("Case") != std::string::npos);
  CHECK(dot.find("In port sel") != std::string::npos);
  CHECK(dot.find("In port a") != std::string::npos);
  CHECK(dot.find("Out port b") != std::string::npos);
}

TEST_CASE("DOT output for sequential state", "[Dot]") {
  auto const &tree = R"(
module m(input logic clk, input logic d, output logic q);
  always_ff @(posedge clk)
    q <= d;
endmodule
)";
  const NetlistTest test(tree);
  auto dot = test.renderDot();
  CHECK(dot.find("In port clk") != std::string::npos);
  CHECK(dot.find("In port d") != std::string::npos);
  CHECK(dot.find("Out port q") != std::string::npos);
  // State nodes are rendered as "name [bounds]".
  CHECK(dot.find("[0:0]") != std::string::npos);
}

TEST_CASE("DOT output with disabled edges", "[Dot]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);

  auto dotBefore = test.renderDot();
  CHECK(dotBefore.find("->") != std::string::npos);

  // Find a labeled edge and disable it.
  bool foundEdge = false;
  std::string disabledEdgeLabel;
  for (auto &node : test.graph) {
    for (auto &edge : node->getOutEdges()) {
      if (!edge->symbol.empty()) {
        disabledEdgeLabel = edge->symbol.name + toString(edge->bounds);
        edge->disable();
        foundEdge = true;
        break;
      }
    }
    if (foundEdge)
      break;
  }
  CHECK(foundEdge);

  auto dotAfter = test.renderDot();
  CHECK(dotAfter.find("digraph") != std::string::npos);
  CHECK(dotAfter.find(disabledEdgeLabel) == std::string::npos);
}

TEST_CASE("DOT output with unlabeled edges", "[Dot]") {
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
  auto dot = test.renderDot();

  // Conditional/Merge branch edges have no symbol label.
  bool hasUnlabeledEdge = false;
  std::istringstream stream(dot);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find("->") != std::string::npos &&
        line.find("label") == std::string::npos) {
      hasUnlabeledEdge = true;
      break;
    }
  }
  CHECK(hasUnlabeledEdge);
}

TEST_CASE("DOT output for module with no logic", "[Dot]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
endmodule
)";
  const NetlistTest test(tree);
  auto dot = test.renderDot();
  CHECK(dot.find("digraph {") != std::string::npos);
  CHECK(dot.find("}") != std::string::npos);
  CHECK(dot.find("In port a") != std::string::npos);
  CHECK(dot.find("Out port b") != std::string::npos);
  CHECK(dot.find("Assignment") == std::string::npos);
  CHECK(dot.find("Conditional") == std::string::npos);
  CHECK(dot.find("Case") == std::string::npos);
}
