#include "Test.hpp"

TEST_CASE("Assigning to a variable", "[SequentialState]") {
  auto const &tree = (R"(
  module m(input clk, input logic a);
    logic b;
    always_ff @(posedge clk)
      b <= a;
  endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="In port a"]
  N3 [label="Assignment"]
  N4 [label="b [0]"]
  N1 -> N4 [label="clk[0]"]
  N2 -> N3 [label="a[0]"]
  N3 -> N4 [label="b[0]"]
}
)");
}

TEST_CASE("Two control paths assigning to the same variable",
          "[SequentialState]") {
  auto const &tree = (R"(
  module m(input clk, input rst, input logic a, output logic b);
    always_ff @(posedge clk or posedge rst)
      if (rst)
        b <= '0;
      else
        b <= a;
  endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="In port rst"]
  N3 [label="In port a"]
  N4 [label="Out port b"]
  N5 [label="Conditional"]
  N6 [label="Assignment"]
  N7 [label="Const 1'b0"]
  N8 [label="Assignment"]
  N9 [label="Merge"]
  N10 [label="b [0]"]
  N1 -> N10 [label="clk[0]"]
  N2 -> N5 [label="rst[0]"]
  N2 -> N10 [label="rst[0]"]
  N3 -> N8 [label="a[0]"]
  N5 -> N6
  N5 -> N8
  N6 -> N9
  N7 -> N6
  N8 -> N9
  N8 -> N10 [label="b[0]"]
  N10 -> N4 [label="b[0]"]
}
)");
}

TEST_CASE("With a self-referential assignment", "[SequentialState]") {
  auto const &tree = (R"(
  module m(input clk, input rst, input logic a, output logic b);
    always_ff @(posedge clk or posedge rst)
      if (rst)
        b <= '0;
      else
        b <= b + a;
endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.pathExists("m.rst", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="In port rst"]
  N3 [label="In port a"]
  N4 [label="Out port b"]
  N5 [label="Conditional"]
  N6 [label="Assignment"]
  N7 [label="Const 1'b0"]
  N8 [label="Assignment"]
  N9 [label="Merge"]
  N10 [label="b [0]"]
  N1 -> N10 [label="clk[0]"]
  N2 -> N5 [label="rst[0]"]
  N2 -> N10 [label="rst[0]"]
  N3 -> N8 [label="a[0]"]
  N5 -> N6
  N5 -> N8
  N6 -> N9
  N7 -> N6
  N8 -> N9
  N8 -> N10 [label="b[0]"]
  N10 -> N4 [label="b[0]"]
  N10 -> N8 [label="b[0]"]
}
)");
}

TEST_CASE("Combinatorial path through sequential state", "[SequentialState]") {
  // A path exists from a to b through the State node, but a combinatorial
  // path does not.
  auto const &tree = (R"(
  module m(input clk, input logic a, output logic b);
    always_ff @(posedge clk)
      b <= a;
  endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK_FALSE(test.combPathExists("m.a", "m.b"));
}

TEST_CASE("Combinatorial path with mixed logic", "[SequentialState]") {
  // A combinatorial path exists from a to x, but the path from a to y goes
  // through sequential state so should not be a combinatorial path.
  auto const &tree = (R"(
  module m(input clk, input logic a, output logic x, output logic y);
    assign x = a;
    always_ff @(posedge clk)
      y <= a;
  endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.combPathExists("m.a", "m.x"));
  CHECK_FALSE(test.combPathExists("m.a", "m.y"));
}

TEST_CASE("Combinatorial path with reset and sequential logic",
          "[SequentialState]") {
  // rst feeds both the sequential always_ff and a combinatorial assign.
  auto const &tree = (R"(
  module m(input clk, input rst, input logic a, output logic b, output logic c);
    always_ff @(posedge clk or posedge rst)
      if (rst)
        b <= '0;
      else
        b <= a;
    assign c = rst;
  endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.pathExists("m.rst", "m.b"));
  CHECK(test.pathExists("m.rst", "m.c"));
  CHECK_FALSE(test.combPathExists("m.a", "m.b"));
  CHECK_FALSE(test.combPathExists("m.rst", "m.b"));
  CHECK(test.combPathExists("m.rst", "m.c"));
}

TEST_CASE("Reference to a previous variable definition", "[SequentialState]") {
  auto const &tree = (R"(
module m(input logic clk, input logic rst, input logic foo, input logic ready, output logic foo_q);
  logic valid_q;
  always @(posedge clk)
    if (rst) begin
      foo_q <= 0;
      valid_q <= 0;
    end else begin
      if (!valid_q)
        foo_q <= foo;
      valid_q <= ready;
    end
endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.foo", "m.foo_q"));
  CHECK(test.pathExists("m.ready", "m.foo_q"));
  CHECK(test.pathExists("m.ready", "m.valid_q"));
  CHECK(test.pathExists("m.rst", "m.valid_q"));
  CHECK(test.pathExists("m.rst", "m.foo_q"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="In port rst"]
  N3 [label="In port foo"]
  N4 [label="In port ready"]
  N5 [label="Out port foo_q"]
  N6 [label="Conditional"]
  N7 [label="Assignment"]
  N8 [label="Const 1'b0"]
  N9 [label="Assignment"]
  N10 [label="Const 1'b0"]
  N11 [label="Conditional"]
  N12 [label="Assignment"]
  N13 [label="Merge"]
  N14 [label="Assignment"]
  N15 [label="Merge"]
  N16 [label="foo_q [0]"]
  N17 [label="valid_q [0]"]
  N1 -> N16 [label="clk[0]"]
  N1 -> N17 [label="clk[0]"]
  N2 -> N6 [label="rst[0]"]
  N3 -> N12 [label="foo[0]"]
  N4 -> N14 [label="ready[0]"]
  N6 -> N7
  N6 -> N11
  N8 -> N7
  N9 -> N15
  N10 -> N9
  N11 -> N12
  N11 -> N13
  N11 -> N14
  N12 -> N13
  N12 -> N16 [label="foo_q[0]"]
  N14 -> N15
  N14 -> N17 [label="valid_q[0]"]
  N16 -> N5 [label="foo_q[0]"]
  N17 -> N11 [label="valid_q[0]"]
}
)");
}

namespace {

// Edges (driverPath -> State stateName) tagged with @p kind.
auto countSensitivityEdges(NetlistGraph const &graph,
                           std::string_view driverPath,
                           std::string_view stateName, ast::EdgeKind kind)
    -> size_t {
  size_t count = 0;
  for (auto const &node : graph) {
    auto path = node->getHierarchicalPath();
    if (!path || *path != driverPath) {
      continue;
    }
    for (auto const &edge : node->getOutEdges()) {
      auto const &target = edge->getTargetNode();
      if (target.kind != NodeKind::State) {
        continue;
      }
      auto targetPath = target.getHierarchicalPath();
      if (!targetPath || *targetPath != stateName) {
        continue;
      }
      if (edge->edgeKind == kind) {
        count++;
      }
    }
  }
  return count;
}

} // namespace

TEST_CASE("Sensitivity: posedge clk drives a single PosEdge edge into State",
          "[SequentialState][Sensitivity]") {
  auto const &tree = R"(
module m(input clk, input logic a, output logic b);
  always_ff @(posedge clk)
    b <= a;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(countSensitivityEdges(test.graph, "m.clk", "m.b",
                              ast::EdgeKind::PosEdge) == 1);
  CHECK(countSensitivityEdges(test.graph, "m.clk", "m.b",
                              ast::EdgeKind::None) == 0);
}

TEST_CASE("Sensitivity: posedge clk + negedge rst_n produces both edges",
          "[SequentialState][Sensitivity]") {
  auto const &tree = R"(
module m(input clk, input rst_n, input logic a, output logic b);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n)
      b <= '0;
    else
      b <= a;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(countSensitivityEdges(test.graph, "m.clk", "m.b",
                              ast::EdgeKind::PosEdge) == 1);
  CHECK(countSensitivityEdges(test.graph, "m.rst_n", "m.b",
                              ast::EdgeKind::NegEdge) == 1);
}

TEST_CASE("Sensitivity: gated clock fans in through its combinational driver",
          "[SequentialState][Sensitivity]") {
  // Clock built from `clk & en`: the sensitivity edge lands on the gating
  // assignment, and clk/en remain reachable upstream.
  auto const &tree = R"(
module m(input clk, input en, input logic a, output logic b);
  wire gclk;
  assign gclk = clk & en;
  always_ff @(posedge gclk)
    b <= a;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.clk", "m.b"));
  CHECK(test.pathExists("m.en", "m.b"));
  CHECK(test.pathExists("m.a", "m.b"));
  // At least one PosEdge edge feeds the State.
  size_t posEdgeIntoState = 0;
  for (auto const &node : test.graph) {
    if (node->kind != NodeKind::State) {
      continue;
    }
    auto path = node->getHierarchicalPath();
    if (!path || *path != "m.b") {
      continue;
    }
    for (auto const &edge : node->getInEdges()) {
      if (edge->edgeKind == ast::EdgeKind::PosEdge) {
        posEdgeIntoState++;
      }
    }
  }
  CHECK(posEdgeIntoState >= 1);
}

TEST_CASE("getSensitivity: returns clk/rst for State node",
          "[SequentialState][Sensitivity]") {
  auto const &tree = R"(
module m(input clk, input rst_n, input logic a, output logic b);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) b <= '0;
    else        b <= a;
endmodule
)";
  const NetlistTest test(tree);
  NetlistNode *stateB = nullptr;
  for (auto const &node : test.graph) {
    if (node->kind == NodeKind::State) {
      auto path = node->getHierarchicalPath();
      if (path && std::string(*path) == "m.b") {
        stateB = node.get();
        break;
      }
    }
  }
  REQUIRE(stateB != nullptr);

  auto sources = test.graph.getSensitivity(*stateB);
  REQUIRE(sources.size() == 2);

  bool sawPosClk = false, sawNegRst = false;
  for (auto const &s : sources) {
    auto path = s.source->getHierarchicalPath();
    REQUIRE(path.has_value());
    std::string p(*path);
    if (p == "m.clk" && s.edgeKind == ast::EdgeKind::PosEdge) {
      sawPosClk = true;
    }
    if (p == "m.rst_n" && s.edgeKind == ast::EdgeKind::NegEdge) {
      sawNegRst = true;
    }
  }
  CHECK(sawPosClk);
  CHECK(sawNegRst);
}

TEST_CASE("getSensitivity: upstream comb node inherits sensitivity from "
          "the State it feeds",
          "[SequentialState][Sensitivity]") {
  // Input feeding a flop through comb logic reports the downstream clock.
  auto const &tree = R"(
module m(input clk, input logic a, input logic b, output logic q);
  logic tmp;
  assign tmp = a ^ b;
  always_ff @(posedge clk)
    q <= tmp;
endmodule
)";
  const NetlistTest test(tree);
  auto *aPort = test.graph.lookup("m.a");
  REQUIRE(aPort != nullptr);

  auto sources = test.graph.getSensitivity(*aPort);
  REQUIRE(sources.size() == 1);
  CHECK(sources[0].edgeKind == ast::EdgeKind::PosEdge);
  auto path = sources[0].source->getHierarchicalPath();
  REQUIRE(path.has_value());
  CHECK(std::string(*path) == "m.clk");
}

TEST_CASE("getSensitivity: pure combinational node returns empty",
          "[SequentialState][Sensitivity]") {
  auto const &tree = R"(
module m(input logic a, input logic b, output logic z);
  assign z = a & b;
endmodule
)";
  const NetlistTest test(tree);
  auto *aPort = test.graph.lookup("m.a");
  REQUIRE(aPort != nullptr);
  CHECK(test.graph.getSensitivity(*aPort).empty());
}

TEST_CASE("getSensitivity: dedupes when one comb node feeds two flops on "
          "the same clock",
          "[SequentialState][Sensitivity]") {
  auto const &tree = R"(
module m(input clk, input logic a, output logic q1, output logic q2);
  logic tmp;
  assign tmp = ~a;
  always_ff @(posedge clk) begin
    q1 <= tmp;
    q2 <= tmp;
  end
endmodule
)";
  const NetlistTest test(tree);
  auto *aPort = test.graph.lookup("m.a");
  REQUIRE(aPort != nullptr);

  // Two State targets, one (clk, PosEdge) entry after dedupe.
  auto sources = test.graph.getSensitivity(*aPort);
  REQUIRE(sources.size() == 1);
  CHECK(sources[0].edgeKind == ast::EdgeKind::PosEdge);
}

TEST_CASE("Sensitivity: combinational always with no edges has no PosEdge "
          "and no State node",
          "[SequentialState][Sensitivity]") {
  // `always @(x or y)` is combinational: no State, no edge-qualified edges.
  auto const &tree = R"(
module m(input logic x, input logic y, output logic z);
  always @(x or y)
    z = x ^ y;
endmodule
)";
  const NetlistTest test(tree);
  for (auto const &node : test.graph) {
    CHECK(node->kind != NodeKind::State);
    for (auto const &edge : node->getOutEdges()) {
      CHECK(edge->edgeKind == ast::EdgeKind::None);
    }
  }
  CHECK(test.pathExists("m.x", "m.z"));
  CHECK(test.pathExists("m.y", "m.z"));
}
