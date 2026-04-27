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
  N2 -> N5 [label="rst[0]"]
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
  N2 -> N5 [label="rst[0]"]
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
