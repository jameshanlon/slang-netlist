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
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N1 -> N3 [label="i_value[0]"]
  N3 -> N5 [label="i_value[0]"]
  N5 -> N4 [label="o_value[0]"]
}
)");
}

TEST_CASE("Instances: basic port connection") {
  auto &tree = R"(
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
  N2 [label="Assignment"]
  N3 [label="In port a"]
  N4 [label="Assignment"]
  N1 -> N3 [label="a[0]"]
  N2 -> N1 [label="a[0]"]
  N3 -> N4 [label="a[0]"]
}
)");
}
