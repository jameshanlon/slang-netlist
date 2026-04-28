#include "Test.hpp"

TEST_CASE("Module instance with connections to the top ports", "[Instance]") {
  auto const &tree = (R"(
module foo(input logic x, input logic y, output logic z);
  assign z = x | y;
endmodule

module top(input logic a, input logic b, output logic c);
  foo u_mux (
    .x(a),
    .y(b),
    .z(c)
  );
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("top.a", "top.c"));
  CHECK(test.pathExists("top.b", "top.c"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="Out port c"]
  N4 [label="In port x"]
  N5 [label="In port y"]
  N6 [label="Out port z"]
  N7 [label="Assignment"]
  N1 -> N4 [label="a[0]"]
  N2 -> N5 [label="b[0]"]
  N4 -> N7 [label="x[0]"]
  N5 -> N7 [label="y[0]"]
  N6 -> N3 [label="c[0]"]
  N7 -> N6 [label="z[0]"]
}
)");
}

TEST_CASE("Signal passthrough with a nested module", "[Instance]") {
  auto const &tree = R"(
module p(input logic i_value, output logic o_value);
  assign o_value = i_value;
endmodule

module m(input logic i_value, output logic o_value);
  p foo(
    .i_value(i_value),
    .o_value(o_value));
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value", "m.o_value"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value"]
  N2 [label="Out port o_value"]
  N3 [label="In port i_value"]
  N4 [label="Out port o_value"]
  N5 [label="Assignment"]
  N1 -> N3 [label="i_value[0]"]
  N3 -> N5 [label="i_value[0]"]
  N4 -> N2 [label="o_value[0]"]
  N5 -> N4 [label="o_value[0]"]
}
)");
}

TEST_CASE("Signal passthrough with a chain of two nested modules",
          "[Instance]") {
  auto const &tree = R"(
 module passthrough(input logic i_value, output logic o_value);
  assign o_value = i_value;
 endmodule

 module m(input logic i_value, output logic o_value);
  logic value;
  passthrough a(
    .i_value(i_value),
    .o_value(value));
  passthrough b(
    .i_value(value),
    .o_value(o_value));
 endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value"]
  N2 [label="Out port o_value"]
  N3 [label="In port i_value"]
  N4 [label="Out port o_value"]
  N5 [label="In port i_value"]
  N6 [label="Out port o_value"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N3 [label="i_value[0]"]
  N3 -> N7 [label="i_value[0]"]
  N4 -> N5 [label="value[0]"]
  N5 -> N8 [label="i_value[0]"]
  N6 -> N2 [label="o_value[0]"]
  N7 -> N4 [label="o_value[0]"]
  N8 -> N6 [label="o_value[0]"]
}
)");
}

// Two instances of the same parameterless module: slang dedupes the body
// (u2 becomes a non-canonical instance pointing back at u1's body for
// driver lookups). The netlist builder still has to materialize a
// distinct set of port nodes, internal wires, and assignment nodes for
// each instance so that per-instance routing remains visible.
TEST_CASE("Two instances of the same module produce independent subgraphs",
          "[Instance]") {
  auto const &tree = R"(
module sub(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule
module m(input logic a, b, e, f, output logic c, d, g, h);
  sub u1(.i({b, a}), .o({d, c}));
  sub u2(.i({f, e}), .o({h, g}));
endmodule
)";
  const NetlistTest test(tree);
  // Bit-precise routing through u1.
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  // Bit-precise routing through u2 — the bug being closed.
  CHECK(test.pathExists("m.e", "m.g"));
  CHECK(test.pathExists("m.f", "m.h"));
  CHECK_FALSE(test.pathExists("m.e", "m.h"));
  CHECK_FALSE(test.pathExists("m.f", "m.g"));
  // No cross-instance leakage.
  CHECK_FALSE(test.pathExists("m.a", "m.g"));
  CHECK_FALSE(test.pathExists("m.e", "m.c"));
}

TEST_CASE("Instances: basic port connection", "[Instance]") {
  auto const &tree = R"(
module foo(output logic a);
  assign a = 1;
endmodule
module bar(input logic a);
  logic b;
  assign b = a;
endmodule
module m();
  logic a;
  foo foo0 (a);
  bar bar0 (a);
endmodule
  )";
  const NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="Out port a"]
  N2 [label="In port a"]
  N3 [label="Assignment"]
  N4 [label="Const 1'b1"]
  N5 [label="Assignment"]
  N1 -> N2 [label="a[0]"]
  N2 -> N5 [label="a[0]"]
  N3 -> N1 [label="a[0]"]
  N4 -> N3
}
)");
}

TEST_CASE("Generate for loop instantiating submodules", "[Instance]") {
  auto const &tree = R"(
module inv(input logic a, output logic b);
  assign b = ~a;
endmodule

module m(input logic [3:0] a, output logic [3:0] b);
  for (genvar i = 0; i < 4; i++) begin : gen
    inv u(.a(a[i]), .b(b[i]));
  end
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}
