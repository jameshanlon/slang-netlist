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
  N1 -> N3 [label="a[0:0]"]
  N3 -> N2 [label="b[0:0]"]
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
  N1 -> N4 [label="a[0:0]"]
  N3 -> N2 [label="b[0:0]"]
  N4 -> N3 [label="temp[0:0]"]
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
  N1 -> N3 [label="i_value[0:0]"]
  N3 -> N4 [label="a[0:0]"]
  N4 -> N5 [label="b[0:0]"]
  N5 -> N6 [label="c[0:0]"]
  N6 -> N7 [label="d[0:0]"]
  N7 -> N8 [label="e[0:0]"]
  N8 -> N2 [label="o_value[0:0]"]
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
  N1 -> N3 [label="a[0:0]"]
  N3 -> N4 [label="pipe[0:0]"]
  N4 -> N5 [label="pipe[1:1]"]
  N5 -> N6 [label="pipe[2:2]"]
  N6 -> N2 [label="b[0:0]"]
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
  N1 -> N5 [label="a[0:0]"]
  N2 -> N4 [label="b[0:0]"]
  N4 -> N6 [label="p[1:1]"]
  N5 -> N6 [label="p[0:0]"]
  N6 -> N7 [label="p[2:2]"]
  N7 -> N3 [label="c[0:0]"]
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
