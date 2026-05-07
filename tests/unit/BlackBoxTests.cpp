#include "Test.hpp"

TEST_CASE("Black-boxing a leaf module breaks paths through it", "[BlackBox]") {
  auto const tree = R"(
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
)";
  BuilderOptions opts;
  opts.blackBoxes = {"foo"};
  NetlistTest test(tree, opts);

  // External wiring still reaches the foo input ports.
  CHECK(test.pathExists("top.a", "top.u_mux.x"));
  CHECK(test.pathExists("top.b", "top.u_mux.y"));
  // The output port still drives the parent's actual.
  CHECK(test.pathExists("top.u_mux.z", "top.c"));
  // But there is no path through foo: its body was not analysed, so
  // the output port is undriven from inside.
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
  CHECK_FALSE(test.pathExists("top.b", "top.c"));
  CHECK_FALSE(test.pathExists("top.u_mux.x", "top.u_mux.z"));
  CHECK_FALSE(test.pathExists("top.u_mux.y", "top.u_mux.z"));
}

TEST_CASE("Black-boxing by hierarchical instance path matches one instance",
          "[BlackBox]") {
  auto const tree = R"(
module passthrough(input logic i, output logic o);
  assign o = i;
endmodule

module top(input logic a, output logic c);
  logic mid;
  passthrough u_a(.i(a),   .o(mid));
  passthrough u_b(.i(mid), .o(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"top.u_a"};
  NetlistTest test(tree, opts);

  // u_a is a black box: no internal path.
  CHECK_FALSE(test.pathExists("top.u_a.i", "top.u_a.o"));
  // u_b is still fully modelled: a path through it exists.
  CHECK(test.pathExists("top.u_b.i", "top.u_b.o"));
  // The whole top.a -> top.c path is broken because u_a is opaque.
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Black-boxing by definition name matches every instance",
          "[BlackBox]") {
  auto const tree = R"(
module passthrough(input logic i, output logic o);
  assign o = i;
endmodule

module top(input logic a, output logic c);
  logic mid;
  passthrough u_a(.i(a),   .o(mid));
  passthrough u_b(.i(mid), .o(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"passthrough"};
  NetlistTest test(tree, opts);

  // Both instances are black-boxed.
  CHECK_FALSE(test.pathExists("top.u_a.i", "top.u_a.o"));
  CHECK_FALSE(test.pathExists("top.u_b.i", "top.u_b.o"));
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Multiple black-box names can be supplied", "[BlackBox]") {
  auto const tree = R"(
module foo(input logic i, output logic o);
  assign o = i;
endmodule

module bar(input logic i, output logic o);
  assign o = i;
endmodule

module baz(input logic i, output logic o);
  assign o = i;
endmodule

module top(input logic a, output logic c);
  logic w1, w2;
  foo u_foo(.i(a),  .o(w1));
  bar u_bar(.i(w1), .o(w2));
  baz u_baz(.i(w2), .o(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"foo", "baz"};
  NetlistTest test(tree, opts);

  // foo and baz are opaque; bar is fully modelled.
  CHECK_FALSE(test.pathExists("top.u_foo.i", "top.u_foo.o"));
  CHECK(test.pathExists("top.u_bar.i", "top.u_bar.o"));
  CHECK_FALSE(test.pathExists("top.u_baz.i", "top.u_baz.o"));
  // End-to-end is broken at the first black box.
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Black box with multi-bit ports preserves boundary connectivity",
          "[BlackBox]") {
  auto const tree = R"(
module bb(input logic [3:0] i, output logic [3:0] o);
  assign o = ~i;
endmodule

module top(input logic [3:0] a, output logic [3:0] c);
  bb u_bb(.i(a), .o(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"bb"};
  NetlistTest test(tree, opts);

  // Each bit on the input side still wires up to the formal port; each
  // bit on the output side still drives the parent's actual.
  CHECK(test.pathExists("top.a", "top.u_bb.i"));
  CHECK(test.pathExists("top.u_bb.o", "top.c"));
  // No internal path.
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Empty black-box list leaves the netlist unchanged", "[BlackBox]") {
  auto const tree = R"(
module foo(input logic x, input logic y, output logic z);
  assign z = x | y;
endmodule

module top(input logic a, input logic b, output logic c);
  foo u_mux(.x(a), .y(b), .z(c));
endmodule
)";
  BuilderOptions opts;
  // No blackBoxes configured.
  NetlistTest test(tree, opts);

  CHECK(test.pathExists("top.a", "top.c"));
  CHECK(test.pathExists("top.b", "top.c"));
}

TEST_CASE("Unmatched black-box name does not affect the netlist",
          "[BlackBox]") {
  auto const tree = R"(
module foo(input logic x, output logic z);
  assign z = x;
endmodule

module top(input logic a, output logic c);
  foo u_foo(.x(a), .z(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"does_not_exist"};
  NetlistTest test(tree, opts);

  CHECK(test.pathExists("top.a", "top.c"));
}
