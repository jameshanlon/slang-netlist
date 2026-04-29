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

// With resolveNonCanonicalInstances enabled, two instances of the same
// module each get their own port nodes and assignment nodes, so concat
// patterns that differ between instances stay routed independently.
TEST_CASE("Two instances of the same module produce independent subgraphs",
          "[Instance]") {
  auto const *tree = R"(
module sub(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule
module m(input logic a, b, e, f, output logic c, d, g, h);
  sub u1(.i({b, a}), .o({d, c}));
  sub u2(.i({f, e}), .o({h, g}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveNonCanonicalInstances = true});
  // Bit-precise routing through u1.
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  // Bit-precise routing through u2 — the case the flag enables.
  CHECK(test.pathExists("m.e", "m.g"));
  CHECK(test.pathExists("m.f", "m.h"));
  CHECK_FALSE(test.pathExists("m.e", "m.h"));
  CHECK_FALSE(test.pathExists("m.f", "m.g"));
  // No cross-instance leakage.
  CHECK_FALSE(test.pathExists("m.a", "m.g"));
  CHECK_FALSE(test.pathExists("m.e", "m.c"));
}

// Multi-instantiated module whose body contains a generate block with
// internal value declarations and logic — but no nested instance.
// populatePairedBodies recurses through GenerateBlockSymbol when
// pairing the two bodies; this test confirms the lockstep stays in
// sync across mixed top-level member kinds (port internals interleaved
// with a generate block) so end-to-end paths complete through both
// instances.
TEST_CASE("Two instances of a module with a generate-block local",
          "[Instance]") {
  auto const *tree = R"(
module sub(input logic i, output logic o);
  if (1) begin : g
    logic mid;
    assign mid = ~i;
    assign o = mid;
  end
endmodule
module m(input logic a, b, output logic c, d);
  sub u1(.i(a), .o(c));
  sub u2(.i(b), .o(d));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveNonCanonicalInstances = true});
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
}

// Nested instance inside a multi-instantiated module. Slang only sets
// a canonical pointer on the outermost non-canonical instance; the
// inner inv inside u2 has none. The structural-pairing pass in
// getCanonicalBody() walks the outer (u2.body, u1.body) pair to derive
// the inner pairing, so the redirect propagates and end-to-end
// connectivity completes through u2 as well.
TEST_CASE("Two instances of a module with a nested instance", "[Instance]") {
  auto const *tree = R"(
module inv(input logic x, output logic y);
  assign y = ~x;
endmodule
module sub(input logic i, output logic o);
  inv u_inv(.x(i), .y(o));
endmodule
module m(input logic a, b, output logic c, d);
  sub u1(.i(a), .o(c));
  sub u2(.i(b), .o(d));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveNonCanonicalInstances = true});
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
}

// Instance array (`sub u[2]`) inside a multi-instantiated module.
// populatePairedBodies must recurse through the InstanceArraySymbol
// scope to pair each element's body with its canonical, otherwise the
// non-canonical element bodies have no driver redirect.
TEST_CASE("Two instances of a module with an instance array", "[Instance]") {
  auto const *tree = R"(
module sub(input logic i, output logic o);
  assign o = ~i;
endmodule
module pair(input logic [1:0] i, output logic [1:0] o);
  sub u[2](.i(i), .o(o));
endmodule
module m(input logic [1:0] a, b, output logic [1:0] c, d);
  pair u1(.i(a), .o(c));
  pair u2(.i(b), .o(d));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveNonCanonicalInstances = true});
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
}

// With the flag at its default (off), only the canonical instance's
// connectivity is wired up; the non-canonical instance has no paths
// from its inputs to its outputs.
TEST_CASE("Two instances: default mode leaves non-canonical instance bare",
          "[Instance]") {
  auto const *tree = R"(
module sub(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule
module m(input logic a, b, e, f, output logic c, d, g, h);
  sub u1(.i({b, a}), .o({d, c}));
  sub u2(.i({f, e}), .o({h, g}));
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.e", "m.g"));
  CHECK_FALSE(test.pathExists("m.f", "m.h"));
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
