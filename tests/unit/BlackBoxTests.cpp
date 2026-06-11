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

TEST_CASE("Glob '*' on definition name matches a prefix family", "[BlackBox]") {
  auto const tree = R"(
module foo_a(input logic i, output logic o);
  assign o = i;
endmodule

module foo_b(input logic i, output logic o);
  assign o = i;
endmodule

module bar(input logic i, output logic o);
  assign o = i;
endmodule

module top(input logic a, output logic c);
  logic w1, w2;
  foo_a u_a(.i(a),  .o(w1));
  foo_b u_b(.i(w1), .o(w2));
  bar   u_c(.i(w2), .o(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"foo_*"};
  NetlistTest test(tree, opts);

  // Both foo_* definitions are opaque; bar is fully modelled.
  CHECK_FALSE(test.pathExists("top.u_a.i", "top.u_a.o"));
  CHECK_FALSE(test.pathExists("top.u_b.i", "top.u_b.o"));
  CHECK(test.pathExists("top.u_c.i", "top.u_c.o"));
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Glob '*' on hierarchical path matches sibling instances",
          "[BlackBox]") {
  auto const tree = R"(
module passthrough(input logic i, output logic o);
  assign o = i;
endmodule

module top(input logic a, output logic c);
  logic w1, w2;
  passthrough u_a(.i(a),  .o(w1));
  passthrough u_b(.i(w1), .o(w2));
  passthrough keep(.i(w2), .o(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"top.u_*"};
  NetlistTest test(tree, opts);

  // u_a and u_b are opaque; keep is still fully modelled.
  CHECK_FALSE(test.pathExists("top.u_a.i", "top.u_a.o"));
  CHECK_FALSE(test.pathExists("top.u_b.i", "top.u_b.o"));
  CHECK(test.pathExists("top.keep.i", "top.keep.o"));
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Glob '?' matches exactly one character", "[BlackBox]") {
  auto const tree = R"(
module passthrough(input logic i, output logic o);
  assign o = i;
endmodule

module top(input logic a, output logic c);
  logic w1, w2;
  passthrough u_a (.i(a),  .o(w1));
  passthrough u_bb(.i(w1), .o(w2));
  passthrough u_c (.i(w2), .o(c));
endmodule
)";
  BuilderOptions opts;
  // 'top.u_?' matches u_a and u_c (single-char suffix) but not u_bb.
  opts.blackBoxes = {"top.u_?"};
  NetlistTest test(tree, opts);

  CHECK_FALSE(test.pathExists("top.u_a.i", "top.u_a.o"));
  CHECK(test.pathExists("top.u_bb.i", "top.u_bb.o"));
  CHECK_FALSE(test.pathExists("top.u_c.i", "top.u_c.o"));
}

TEST_CASE("Glob pattern that matches nothing leaves the netlist unchanged",
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
  opts.blackBoxes = {"nope_*", "??_does_not_match"};
  NetlistTest test(tree, opts);

  CHECK(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Glob '*' is single-segment in hierarchical paths", "[BlackBox]") {
  auto const tree = R"(
module leaf(input logic i, output logic o);
  assign o = i;
endmodule

module mid(input logic i, output logic o);
  leaf u_leaf(.i(i), .o(o));
endmodule

module top(input logic a, output logic c);
  mid u_mid(.i(a), .o(c));
endmodule
)";
  BuilderOptions opts;
  // 'top.*' is single-segment; it only matches top's direct children
  // (u_mid), not the nested leaf instance.
  opts.blackBoxes = {"top.*"};
  NetlistTest test(tree, opts);

  // u_mid is opaque, so its body (containing u_leaf) is not visited at
  // all and no leaf instance exists in the netlist. The end-to-end path
  // is therefore broken.
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Glob '**' crosses '.' in hierarchical paths", "[BlackBox]") {
  auto const tree = R"(
module leaf(input logic i, output logic o);
  assign o = i;
endmodule

module mid(input logic i, output logic o);
  leaf u_leaf(.i(i), .o(o));
endmodule

module top(input logic a, output logic c);
  mid u_mid(.i(a), .o(c));
endmodule
)";
  BuilderOptions opts;
  // 'top.**.u_leaf' should reach across the mid hierarchy and match the
  // nested leaf instance.
  opts.blackBoxes = {"top.**.u_leaf"};
  NetlistTest test(tree, opts);

  CHECK_FALSE(test.pathExists("top.u_mid.u_leaf.i", "top.u_mid.u_leaf.o"));
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}

TEST_CASE("Coverage: boundary ports and outside nodes", "[BlackBox]") {
  auto const tree = R"(
module foo(input logic x, input logic y, output logic z);
  assign z = x | y;
endmodule

module top(input logic a, input logic b, output logic c);
  foo u_mux(.x(a), .y(b), .z(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"foo"};
  NetlistTest test(tree, opts);

  // The resolved instance path is recorded on the graph.
  auto paths = test.graph.getBlackBoxPaths();
  REQUIRE(paths.size() == 1);
  CHECK(paths[0] == "top.u_mux");

  // Ports of the black-boxed instance are on the boundary.
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.u_mux.x")) ==
        BlackBoxCoverage::Boundary);
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.u_mux.y")) ==
        BlackBoxCoverage::Boundary);
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.u_mux.z")) ==
        BlackBoxCoverage::Boundary);

  // Nodes in the parent scope are outside.
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.a")) ==
        BlackBoxCoverage::Outside);
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.c")) ==
        BlackBoxCoverage::Outside);
}

TEST_CASE("Coverage: definition-name pattern records one path per instance",
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

  auto paths = test.graph.getBlackBoxPaths();
  REQUIRE(paths.size() == 2);
  CHECK(std::ranges::count(paths, "top.u_a") == 1);
  CHECK(std::ranges::count(paths, "top.u_b") == 1);

  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.u_a.i")) ==
        BlackBoxCoverage::Boundary);
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.u_b.o")) ==
        BlackBoxCoverage::Boundary);
}

TEST_CASE("Coverage: prefix matching is segment-aware", "[BlackBox]") {
  auto const tree = R"(
module passthrough(input logic i, output logic o);
  assign o = i;
endmodule

module top(input logic a, output logic c);
  logic mid;
  passthrough u_a (.i(a),   .o(mid));
  passthrough u_ab(.i(mid), .o(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"top.u_a"};
  NetlistTest test(tree, opts);

  // 'top.u_a' must not cover 'top.u_ab.*'.
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.u_a.i")) ==
        BlackBoxCoverage::Boundary);
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.u_ab.i")) ==
        BlackBoxCoverage::Outside);
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.u_ab.o")) ==
        BlackBoxCoverage::Outside);
}

TEST_CASE("Coverage: nodes without hierarchical paths are outside",
          "[BlackBox]") {
  auto const tree = R"(
module foo(input logic x, output logic z);
  assign z = x;
endmodule

module top(input logic a, output logic c);
  logic w;
  assign w = a;
  foo u_foo(.x(w), .z(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"foo"};
  NetlistTest test(tree, opts);

  for (auto const &node : test.graph.filterNodes(NodeKind::Assignment)) {
    CHECK(test.graph.getBlackBoxCoverage(*node) == BlackBoxCoverage::Outside);
  }
}

TEST_CASE("Coverage: contained nodes under a manually added path",
          "[BlackBox]") {
  auto const tree = R"(
module leaf(input logic i, output logic o);
  assign o = i;
endmodule

module mid(input logic clk, input logic i, output logic o);
  logic q;
  always_ff @(posedge clk) q <= i;
  leaf u_leaf(.i(q), .o(o));
endmodule

module top(input logic clk, input logic a, output logic c);
  mid u_mid(.clk(clk), .i(a), .o(c));
endmodule
)";
  // Build the full graph (no build-time black-boxing), then register the
  // box afterwards, as a deserialized or query-time consumer would.
  NetlistTest test(tree);
  test.graph.addBlackBoxPath("top.u_mid");

  auto coverage = [&](std::string const &name) {
    auto *node = test.graph.lookup(name);
    REQUIRE(node != nullptr);
    return test.graph.getBlackBoxCoverage(*node);
  };

  // Ports of the box are on the boundary.
  CHECK(coverage("top.u_mid.i") == BlackBoxCoverage::Boundary);
  CHECK(coverage("top.u_mid.o") == BlackBoxCoverage::Boundary);

  // A non-port node directly under the box is contained, as is anything
  // deeper, including ports of nested instances.
  CHECK(coverage("top.u_mid.q") == BlackBoxCoverage::Contained);
  CHECK(coverage("top.u_mid.u_leaf.i") == BlackBoxCoverage::Contained);

  // Parent-scope nodes are outside.
  CHECK(coverage("top.a") == BlackBoxCoverage::Outside);
}

TEST_CASE("Coverage: no black boxes means everything is outside",
          "[BlackBox]") {
  auto const tree = R"(
module top(input logic a, output logic c);
  assign c = a;
endmodule
)";
  NetlistTest test(tree);

  CHECK(test.graph.getBlackBoxPaths().empty());
  CHECK(test.graph.getBlackBoxCoverage(*test.graph.lookup("top.a")) ==
        BlackBoxCoverage::Outside);
}

TEST_CASE("Glob '...' is equivalent to '**' across path boundaries",
          "[BlackBox]") {
  auto const tree = R"(
module leaf(input logic i, output logic o);
  assign o = i;
endmodule

module mid(input logic i, output logic o);
  leaf u_leaf(.i(i), .o(o));
endmodule

module top(input logic a, output logic c);
  mid u_mid(.i(a), .o(c));
endmodule
)";
  BuilderOptions opts;
  opts.blackBoxes = {"top....u_leaf"};
  NetlistTest test(tree, opts);

  CHECK_FALSE(test.pathExists("top.u_mid.u_leaf.i", "top.u_mid.u_leaf.o"));
  CHECK_FALSE(test.pathExists("top.a", "top.c"));
}
