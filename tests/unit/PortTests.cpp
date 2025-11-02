#include "Test.hpp"

TEST_CASE("Multiple assignments to an output port", "[Ports]") {
  auto const &tree = (R"(
module m(input in, output [1:0] out);
   assign out[0] = in;
   assign out[1] = in;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.in", "m.out"));
}

TEST_CASE("Multiple assignments from an input port", "[Ports]") {
  auto const &tree = (R"(
module m(input [1:0] in, output out);
   assign out = {in[0], in[1]};
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.in", "m.out"));
}

TEST_CASE("Multiple assignments to internal port", "[Ports]") {
  auto const &tree = R"(
module foo(output logic [1:0] out);
  assign out[0] = 1'b0;
  assign out[1] = 1'b1;
endmodule
module bar(input logic [1:0] in);
  logic a;
  logic b;
  assign a = in[0];
  assign b = in[1];
endmodule
module m();
  logic [1:0] baz;
  foo u_foo(.out(baz));
  bar u_bar(.in(baz));
endmodule
)";
  const NetlistTest test(tree);
  // Internal signal 'baz' has two drivers.
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="Out port out"]
  N2 [label="Out port out"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="In port in"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N1 -> N5 [label="baz[1:0]"]
  N2 -> N5 [label="baz[1:0]"]
  N3 -> N1 [label="out[0:0]"]
  N4 -> N2 [label="out[1:1]"]
  N5 -> N6 [label="in[0:0]"]
  N5 -> N7 [label="in[1:1]"]
}
)");
}

TEST_CASE("Registered output port", "[Ports]") {
  auto const &tree = (R"(
module m(input logic clk, input logic rst, input logic foo, output logic foo_q);
  always_ff @(posedge clk or posedge rst)
    if (rst)
      foo_q <= 0;
    else
      foo_q <= foo;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.foo", "m.foo_q"));
  CHECK(test.pathExists("m.rst", "m.foo_q"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="In port rst"]
  N3 [label="In port foo"]
  N4 [label="Out port foo_q"]
  N5 [label="Conditional"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N9 [label="foo_q [0:0]"]
  N2 -> N5 [label="rst[0:0]"]
  N3 -> N7 [label="foo[0:0]"]
  N5 -> N6
  N5 -> N7
  N6 -> N8
  N7 -> N8
  N7 -> N9 [label="foo_q[0:0]"]
  N9 -> N4 [label="foo_q[0:0]"]
}
)");
}
