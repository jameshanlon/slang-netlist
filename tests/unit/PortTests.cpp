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
  N3 [label="In port in"]
  N4 [label="Assignment"]
  N5 [label="Const 1'b0"]
  N6 [label="Assignment"]
  N7 [label="Const 1'b1"]
  N8 [label="Assignment"]
  N9 [label="Assignment"]
  N1 -> N3 [label="baz[0]"]
  N2 -> N3 [label="baz[1]"]
  N3 -> N8 [label="in[0]"]
  N3 -> N9 [label="in[1]"]
  N4 -> N1 [label="out[0]"]
  N5 -> N4
  N6 -> N2 [label="out[1]"]
  N7 -> N6
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
  N7 [label="Const 1'b0"]
  N8 [label="Assignment"]
  N9 [label="Merge"]
  N10 [label="foo_q [0]"]
  N2 -> N5 [label="rst[0]"]
  N3 -> N8 [label="foo[0]"]
  N5 -> N6
  N5 -> N8
  N6 -> N9
  N7 -> N6
  N8 -> N9
  N8 -> N10 [label="foo_q[0]"]
  N10 -> N4 [label="foo_q[0]"]
}
)");
}

TEST_CASE("Variable with multiple port back-references", "[Ports]") {
  // Exercises hookupOutputPort's early return for symbols with multiple
  // port back-references: two explicit output ports alias the same
  // internal variable, producing two PortBackref entries.
  auto const &tree = R"(
module m(.a(x), .b(x));
  output logic x;
  assign x = 1;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.graph.numNodes() > 0);
}
