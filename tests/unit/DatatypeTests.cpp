#include "Test.hpp"

TEST_CASE("Assign to different slices of a vector", "[Datatype]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic [1:0] y);
  logic [1:0] t;
  always_comb begin
    t[0] = a;
    t[1] = b;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  // Both a and b should be valid paths to y.
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Chain of dependencies through a packed array", "[Datatype]") {
  auto const &tree = R"(
module m(input logic i_value, output logic o_value);
  logic [4:0] x;
  assign x[0] = i_value;
  always_comb begin
    x[1] = x[0];
    x[2] = x[1];
    x[3] = x[2];
  end
  assign x[4] = x[3];
  assign o_value = x[4];
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
  N3 -> N4 [label="x[0]"]
  N4 -> N5 [label="x[1]"]
  N5 -> N6 [label="x[2]"]
  N6 -> N7 [label="x[3]"]
  N7 -> N8 [label="x[4]"]
  N8 -> N2 [label="o_value[0]"]
}
)");
}

TEST_CASE("Passthrough two signals via ranges in a shared vector",
          "[Datatype]") {
  auto const &tree = R"(
module m(
  input  logic [1:0] i_value_a,
  input  logic [1:0] i_value_b,
  output logic [1:0] o_value_a,
  output logic [1:0] o_value_b);
  logic [3:0] foo;
  assign foo[1:0] = i_value_a;
  assign foo[3:2] = i_value_b;
  assign o_value_a = foo[1:0];
  assign o_value_b = foo[3:2];
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value_a", "m.o_value_a"));
  CHECK(test.pathExists("m.i_value_b", "m.o_value_b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value_a"]
  N2 [label="In port i_value_b"]
  N3 [label="Out port o_value_a"]
  N4 [label="Out port o_value_b"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N5 [label="i_value_a[1:0]"]
  N2 -> N6 [label="i_value_b[1:0]"]
  N5 -> N7 [label="foo[1:0]"]
  N6 -> N8 [label="foo[3:2]"]
  N7 -> N3 [label="o_value_a[1:0]"]
  N8 -> N4 [label="o_value_b[1:0]"]
}
)");
}

TEST_CASE("Passthrough two signals via a shared struct", "[Datatype]") {
  auto const &tree = R"(
module m(
  input logic i_value_a,
  input logic i_value_b,
  output logic o_value_a,
  output logic o_value_b);
  struct packed {
    logic a;
    logic b;
  } foo;
  assign foo.a = i_value_a;
  assign foo.b = i_value_b;
  assign o_value_a = foo.a;
  assign o_value_b = foo.b;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value_a", "m.o_value_a"));
  CHECK(test.pathExists("m.i_value_b", "m.o_value_b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value_a"]
  N2 [label="In port i_value_b"]
  N3 [label="Out port o_value_a"]
  N4 [label="Out port o_value_b"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N5 [label="i_value_a[0]"]
  N2 -> N6 [label="i_value_b[0]"]
  N5 -> N7 [label="foo[1]"]
  N6 -> N8 [label="foo[0]"]
  N7 -> N3 [label="o_value_a[0]"]
  N8 -> N4 [label="o_value_b[0]"]
}
)");
}

TEST_CASE("Passthrough two signals via a shared union", "[Datatype]") {
  auto const &tree = R"(
module m(input logic i_value_a,
         input logic i_value_b,
         output logic o_value_a,
         output logic o_value_b,
         output logic o_value_c);
  union packed {
    logic [1:0] a;
    logic [1:0] b;
  } foo;
  assign foo.a[0] = i_value_a;
  assign foo.b[1] = i_value_b;
  assign o_value_a = foo.a[0];
  assign o_value_b = foo.b[1];
  assign o_value_c = foo.b[0]; // Overlapping with a in union.
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value_a", "m.o_value_a"));
  CHECK(test.pathExists("m.i_value_b", "m.o_value_b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value_a"]
  N2 [label="In port i_value_b"]
  N3 [label="Out port o_value_a"]
  N4 [label="Out port o_value_b"]
  N5 [label="Out port o_value_c"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N9 [label="Assignment"]
  N10 [label="Assignment"]
  N1 -> N6 [label="i_value_a[0]"]
  N2 -> N7 [label="i_value_b[0]"]
  N6 -> N8 [label="foo[0]"]
  N6 -> N10 [label="foo[0]"]
  N7 -> N9 [label="foo[1]"]
  N8 -> N3 [label="o_value_a[0]"]
  N9 -> N4 [label="o_value_b[0]"]
  N10 -> N5 [label="o_value_c[0]"]
}
)");
}

TEST_CASE("Automatic variables are skipped", "[Datatype]") {
  auto const &tree = (R"(
module m(input logic a, output logic b);
  logic t;
  always_comb begin
    automatic int l;
    t = a;
    l = t;
    b = l;
  end
endmodule
)");
  const NetlistTest test(tree);
  CHECK(!test.pathExists("m.a", "m.b"));
}
