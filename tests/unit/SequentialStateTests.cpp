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
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N9 [label="b [0]"]
  N2 -> N5 [label="rst[0]"]
  N3 -> N7 [label="a[0]"]
  N5 -> N6
  N5 -> N7
  N6 -> N8
  N7 -> N8
  N7 -> N9 [label="b[0]"]
  N9 -> N4 [label="b[0]"]
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
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N9 [label="b [0]"]
  N2 -> N5 [label="rst[0]"]
  N3 -> N7 [label="a[0]"]
  N5 -> N6
  N5 -> N7
  N6 -> N8
  N7 -> N8
  N7 -> N9 [label="b[0]"]
  N9 -> N4 [label="b[0]"]
  N9 -> N7 [label="b[0]"]
}
)");
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
  N8 [label="Assignment"]
  N9 [label="Conditional"]
  N10 [label="Assignment"]
  N11 [label="Merge"]
  N12 [label="Assignment"]
  N13 [label="Merge"]
  N14 [label="valid_q [0]"]
  N15 [label="foo_q [0]"]
  N2 -> N6 [label="rst[0]"]
  N3 -> N10 [label="foo[0]"]
  N4 -> N12 [label="ready[0]"]
  N6 -> N7
  N6 -> N9
  N8 -> N13
  N9 -> N10
  N9 -> N11
  N9 -> N12
  N10 -> N11
  N10 -> N15 [label="foo_q[0]"]
  N12 -> N13
  N12 -> N14 [label="valid_q[0]"]
  N14 -> N9 [label="valid_q[0]"]
  N15 -> N5 [label="foo_q[0]"]
}
)");
}
