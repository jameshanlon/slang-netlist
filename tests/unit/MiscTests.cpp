#include "Test.hpp"

TEST_CASE("Empty module", "[Netlist]") {
  auto const &tree = (R"(
module m();
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.graph.numNodes() == 0);
  CHECK(test.graph.numEdges() == 0);
}

TEST_CASE("Passthrough module", "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N1 -> N3 [label="a[0]"]
  N3 -> N2 [label="b[0]"]
}
)");
}

TEST_CASE("Module with out-of-order dependencies", "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, output logic b);
  logic temp;
  assign b = temp;
  assign temp = a;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N1 -> N4 [label="a[0]"]
  N3 -> N2 [label="b[0]"]
  N4 -> N3 [label="temp[0]"]
}
)");
}

TEST_CASE("Chained assignments", "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t, u;
  always_comb begin
    t = a;
    u = t;
  end
  assign y = u;
endmodule
  )");
  const NetlistTest test(tree);
  // a should be a valid path to y through t and u.
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Chain of dependencies through procedural and continuous assignments",
          "[Netlist]") {
  auto const &tree = R"(
module m(input logic i_value, output logic o_value);
  logic a, b, c, d, e;
  assign a = i_value;
  always_comb begin
    b = a;
    c = b;
    d = c;
  end
  assign e = d;
  assign o_value = e;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value"]
  N2 [label="Out port o_value"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N3 [label="i_value[0]"]
  N3 -> N4 [label="a[0]"]
  N4 -> N5 [label="b[0]"]
  N5 -> N6 [label="c[0]"]
  N6 -> N7 [label="d[0]"]
  N7 -> N8 [label="e[0]"]
  N8 -> N2 [label="o_value[0]"]
}
)");
}

TEST_CASE("Chain of dependencies though continuous assignments", "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, output logic b);
  logic [2:0] pipe;
  assign pipe[0] = a;
  assign pipe[1] = pipe[0];
  assign pipe[2] = pipe[1];
  assign b = pipe[2];
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N1 -> N3 [label="a[0]"]
  N3 -> N4 [label="pipe[0]"]
  N4 -> N5 [label="pipe[1]"]
  N5 -> N6 [label="pipe[2]"]
  N6 -> N2 [label="b[0]"]
}
)");
}

TEST_CASE("Procedural statement with internal and external r-values",
          "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic c);
  logic [2:0] p;
  assign p[1] = b;
  always_comb begin
    p[0] = a;
    p[2] = p[0] + p[1];
  end
  assign c = p[2];
endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.c"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="Out port c"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N1 -> N5 [label="a[0]"]
  N2 -> N4 [label="b[0]"]
  N4 -> N6 [label="p[1]"]
  N5 -> N6 [label="p[0]"]
  N6 -> N7 [label="p[2]"]
  N7 -> N3 [label="c[0]"]
}
)");
}

TEST_CASE("Sequential (blocking) assignment overwrites previous value",
          "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t;
  always_comb begin
    t = a;
    t = b;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  // Only b should be a valid path to y, a should not.
  CHECK(!test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Overlapping assignments to same variable", "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic [1:0] y);
  logic [1:0] t;
  always_comb begin
    t[1:0] = a;
    t[0] = b;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  // b should be the only driver for t[0], and a for t[1].
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Procedural force statement", "[Netlist]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  logic t;
  assign t = a;
  initial begin
    force b = t;
  end
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.graph.numNodes() > 0);
}

TEST_CASE("Uninstantiated instance is skipped", "[Netlist]") {
  // An uninstantiated module should be skipped during graph construction
  // (NetlistBuilder::handle(InstanceSymbol)).
  auto const &tree = R"(
module inner(input logic a, output logic b);
  assign b = a;
endmodule

module m(input logic x, output logic y);
  assign y = x;
  if (0) begin : gen
    inner u(.a(x), .b(y));
  end
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.x", "m.y"));
}

TEST_CASE("Procedural assign (non-force)", "[Netlist]") {
  // Procedural `assign` (not force) exercises the else branch of
  // DataFlowAnalysis::handle(ProceduralAssignStatement).
  auto const &tree = R"(
module m(input logic a, output logic b);
  reg t;
  initial begin
    assign t = a;
  end
  assign b = t;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.graph.numNodes() > 0);
}

TEST_CASE("Unreachable else branch with constant condition", "[Netlist]") {
  // When a condition is constant, one branch is unreachable. This exercises
  // the meetState path (DataFlowAnalysis) where other.reachable
  // is false.
  auto const &tree = R"(
module m(input logic a, output logic b);
  always_comb begin
    if (1)
      b = a;
    else
      b = 1'b0;
  end
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}

TEST_CASE("always_latch inferred latch", "[Netlist]") {
  auto const &tree = R"(
module m(input logic en, input logic d, output logic q);
  always_latch
    if (en)
      q <= d;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.d", "m.q"));
  CHECK(test.pathExists("m.en", "m.q"));
}

TEST_CASE("Non-contiguous bit selects of the same variable", "[Netlist]") {
  // Reading non-contiguous bits of the same variable in a single expression
  // exercises the parallel-edge path in addDependency (sequential mode): the
  // first pending R-value creates an edge annotated with one bit range, and
  // the second pending R-value finds that edge but cannot merge its
  // non-contiguous range, so a second parallel edge is created.
  auto const &tree = R"(
module m(input logic [3:0] a, output logic y);
  assign y = a[0] ^ a[3];
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.y"));
  // The assignment node should have two incoming edges from port a, one for
  // bit 0 and one for bit 3.
  auto assignView = test.graph.filterNodes(NodeKind::Assignment);
  REQUIRE_FALSE(assignView.empty());
  auto &assignNode = *assignView.front();
  // Count incoming edges from the port node.
  auto *portNode = test.graph.lookup("m.a");
  REQUIRE(portNode != nullptr);
  size_t edgesFromPort = 0;
  for (auto &edge : portNode->getOutEdges()) {
    if (&edge->getTargetNode() == &assignNode) {
      edgesFromPort++;
    }
  }
  CHECK(edgesFromPort == 2);
}

TEST_CASE("Uninstantiated generate block is skipped", "[Netlist]") {
  // An uninstantiated generate block (handle(GenerateBlockSymbol) with
  // isUninstantiated == true) should contribute no nodes to the graph.
  auto const &tree = R"(
module m #(parameter int MODE = 0)(input logic a, output logic y);
  generate
    if (MODE == 1) begin : active
      assign y = a;
    end else begin : fallback
      assign y = ~a;
    end
  endgenerate
endmodule
)";
  const NetlistTest test(tree);
  // Only the fallback branch should be instantiated.
  CHECK(test.pathExists("m.a", "m.y"));
  // The active branch should not generate any extra assignments.
  auto assignView2 = test.graph.filterNodes(NodeKind::Assignment);
  int assignCount = 0;
  for (auto &n : assignView2) {
    (void)n;
    assignCount++;
  }
  CHECK(assignCount == 1);
}
