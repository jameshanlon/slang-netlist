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

// Two instances of the same module each get their own port nodes and
// assignment nodes, so concat patterns that differ between instances
// stay routed independently.
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
  NetlistTest test(tree);
  // Bit-precise routing through u1.
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  // Bit-precise routing through u2.
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
  NetlistTest test(tree);
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
  NetlistTest test(tree);
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
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
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

// Three or more instances of the same module: every non-canonical body
// must redirect ValueSymbol lookups to the canonical body so each
// instance produces independent end-to-end paths.
TEST_CASE("Three instances of the same module produce independent paths",
          "[Instance]") {
  auto const *tree = R"(
module sub(input logic i, output logic o);
  assign o = ~i;
endmodule

module m(input logic a, b, c, output logic x, y, z);
  sub u1(.i(a), .o(x));
  sub u2(.i(b), .o(y));
  sub u3(.i(c), .o(z));
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.c", "m.z"));
  // No cross-instance leakage.
  CHECK_FALSE(test.pathExists("m.a", "m.y"));
  CHECK_FALSE(test.pathExists("m.b", "m.z"));
  CHECK_FALSE(test.pathExists("m.c", "m.x"));
  CHECK_FALSE(test.pathExists("m.a", "m.z"));
  // Driver query against the non-canonical hierarchical paths should
  // each return a node — the redirect resolves both u2 and u3 (the
  // non-canonical instances) back to drivers stored under u1's body.
  CHECK(test.getDrivers("m.u1.o", {0, 0}).size() >= 1);
  CHECK(test.getDrivers("m.u2.o", {0, 0}).size() >= 1);
  CHECK(test.getDrivers("m.u3.o", {0, 0}).size() >= 1);
}

// Two instances of a module whose body contains an always_ff register.
// Sequential drivers stored under the canonical body must still
// resolve through each non-canonical instance, producing distinct
// per-instance State and Variable nodes.
TEST_CASE("Two instances of a module with always_ff register", "[Instance]") {
  auto const *tree = R"(
module sub(input logic clk, input logic in, output logic out);
  always_ff @(posedge clk) out <= in;
endmodule

module m(input logic clk, a, b, output logic c, d);
  sub u1(.clk(clk), .in(a), .out(c));
  sub u2(.clk(clk), .in(b), .out(d));
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  // Sequential drivers under the non-canonical instance hierarchical
  // path are resolvable; the two output nodes are distinct.
  auto u1Drivers = test.getDrivers("m.u1.out", {0, 0});
  auto u2Drivers = test.getDrivers("m.u2.out", {0, 0});
  REQUIRE(!u1Drivers.empty());
  REQUIRE(!u2Drivers.empty());
  CHECK(u1Drivers[0] != u2Drivers[0]);
}

// Three-deep nested instances inside a multi-instantiated module.
// Slang only stores a canonical pointer on the outermost
// non-canonical instance; populatePairedBodies must recurse to derive
// the inner pairings (mid -> mid, leaf -> leaf) so per-bit drivers
// resolve through all three layers.
TEST_CASE("Two instances of a three-deep nested instance hierarchy",
          "[Instance]") {
  auto const *tree = R"(
module leaf(input logic x, output logic y);
  assign y = ~x;
endmodule

module mid(input logic mi, output logic mo);
  leaf u(.x(mi), .y(mo));
endmodule

module sub(input logic si, output logic so);
  mid u(.mi(si), .mo(so));
endmodule

module m(input logic a, b, output logic c, d);
  sub u1(.si(a), .so(c));
  sub u2(.si(b), .so(d));
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  // Driver query through three layers of redirect for the non-canonical
  // instance must succeed.
  CHECK(test.getDrivers("m.u2.u.u.y", {0, 0}).size() >= 1);
  CHECK(test.getDrivers("m.u1.u.u.y", {0, 0}).size() >= 1);
}

// Two instances of a module that itself contains two named submodule
// instances. Driver lookup via the non-canonical outer instance's
// hierarchical path must resolve every nested sibling, not just the
// first — populatePairedBodies chases ci.getCanonicalBody() so a
// sibling whose body slang collapsed onto a different canonical leaf
// is reached transitively.
TEST_CASE("Two instances of a module containing an inner multi-instance pair",
          "[Instance]") {
  auto const *tree = R"(
module leaf(input logic x, output logic y);
  assign y = x;
endmodule

module sub(input logic [1:0] i, output logic [1:0] o);
  leaf l0(.x(i[0]), .y(o[0]));
  leaf l1(.x(i[1]), .y(o[1]));
endmodule

module m(input logic [1:0] a, b, output logic [1:0] c, d);
  sub u1(.i(a), .o(c));
  sub u2(.i(b), .o(d));
endmodule
)";
  NetlistTest test(tree);
  // End-to-end paths and per-bit output drivers.
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  CHECK(test.getDrivers("m.d", {0, 0}).size() >= 1);
  CHECK(test.getDrivers("m.d", {1, 1}).size() >= 1);
  // Canonical and non-canonical paths both resolve every nested leaf.
  CHECK(test.getDrivers("m.u1.l0.y", {0, 0}).size() >= 1);
  CHECK(test.getDrivers("m.u1.l1.y", {0, 0}).size() >= 1);
  CHECK(test.getDrivers("m.u2.l0.y", {0, 0}).size() >= 1);
  CHECK(test.getDrivers("m.u2.l1.y", {0, 0}).size() >= 1);
}

// Multi-instance module with a case statement inside an always_comb
// block. Each non-canonical body's combinational driver tree must
// resolve independently — drivers stored on the canonical body's case
// arms still produce per-instance edges through both u1 and u2.
TEST_CASE("Two instances with always_comb case-driven outputs", "[Instance]") {
  auto const *tree = R"(
module sub(input logic s, input logic [1:0] a, b, output logic [1:0] o);
  always_comb begin
    case (s)
      1'b0: o = a;
      default: o = b;
    endcase
  end
endmodule

module m(input logic s1, s2,
         input logic [1:0] x, y, p, q,
         output logic [1:0] r1, r2);
  sub u1(.s(s1), .a(x), .b(y), .o(r1));
  sub u2(.s(s2), .a(p), .b(q), .o(r2));
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.x", "m.r1"));
  CHECK(test.pathExists("m.y", "m.r1"));
  CHECK(test.pathExists("m.s1", "m.r1"));
  CHECK(test.pathExists("m.p", "m.r2"));
  CHECK(test.pathExists("m.q", "m.r2"));
  CHECK(test.pathExists("m.s2", "m.r2"));
  // No cross-instance leakage.
  CHECK_FALSE(test.pathExists("m.x", "m.r2"));
  CHECK_FALSE(test.pathExists("m.p", "m.r1"));
  CHECK_FALSE(test.pathExists("m.s1", "m.r2"));
  CHECK_FALSE(test.pathExists("m.s2", "m.r1"));
}

// Multi-instanced module whose body contains an instance array AND
// nested generate blocks. Exercises populatePairedBodies recursion
// through both InstanceArraySymbol and GenerateBlockSymbol scopes in
// the same body.
TEST_CASE("Two instances containing instance array inside generate block",
          "[Instance]") {
  auto const *tree = R"(
module leaf(input logic x, output logic y);
  assign y = x;
endmodule

module sub(input logic [1:0] i, output logic [1:0] o);
  if (1) begin : g
    leaf u[2](.x(i), .y(o));
  end
endmodule

module m(input logic [1:0] a, b, output logic [1:0] c, d);
  sub u1(.i(a), .o(c));
  sub u2(.i(b), .o(d));
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
}

// Multi-instance module whose canonical body assigns to a vector via a
// chain of bit-aligned partial drives. Each non-canonical instance
// must surface the same per-bit driver structure independently.
TEST_CASE("Two instances with bit-aligned partial drives in always_comb",
          "[Instance]") {
  auto const *tree = R"(
module sub(input logic [3:0] in, output logic [3:0] out);
  always_comb begin
    out[1:0] = in[3:2];
    out[3:2] = in[1:0];
  end
endmodule

module m(input logic [3:0] a, b, output logic [3:0] c, d);
  sub u1(.in(a), .out(c));
  sub u2(.in(b), .out(d));
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  // Per-bit drivers resolve through both hierarchical paths.
  CHECK(test.getDrivers("m.u1.out", {0, 0}).size() >= 1);
  CHECK(test.getDrivers("m.u2.out", {0, 0}).size() >= 1);
}

// Nested instances combined with multi-instantiation at every level.
// Every port — top, mid, and leaf — must be present in the graph and
// reachable via its full hierarchical path through `graph.lookup()`,
// with the right direction and bounds. Both canonical and non-canonical
// hierarchical prefixes (m.u1.* and m.u2.*) must resolve.
TEST_CASE("Ports of nested multi-instances are looked up by hierarchical path",
          "[Instance]") {
  auto const *tree = R"(
module leaf(input logic x, output logic y);
  assign y = ~x;
endmodule

module mid(input logic [1:0] mi, output logic [1:0] mo);
  leaf l0(.x(mi[0]), .y(mo[0]));
  leaf l1(.x(mi[1]), .y(mo[1]));
endmodule

module top(input logic [1:0] a, b, output logic [1:0] c, d);
  mid u1(.mi(a), .mo(c));
  mid u2(.mi(b), .mo(d));
endmodule
)";
  NetlistTest test(tree);

  struct Expected {
    std::string path;
    ast::ArgumentDirection direction;
    DriverBitRange bounds;
  };

  // Every port at every hierarchical path in the design.
  std::vector<Expected> expected = {
      // top-level ports
      {"top.a", ast::ArgumentDirection::In, {0, 1}},
      {"top.b", ast::ArgumentDirection::In, {0, 1}},
      {"top.c", ast::ArgumentDirection::Out, {0, 1}},
      {"top.d", ast::ArgumentDirection::Out, {0, 1}},
      // mid instance ports
      {"top.u1.mi", ast::ArgumentDirection::In, {0, 1}},
      {"top.u1.mo", ast::ArgumentDirection::Out, {0, 1}},
      {"top.u2.mi", ast::ArgumentDirection::In, {0, 1}},
      {"top.u2.mo", ast::ArgumentDirection::Out, {0, 1}},
      // leaf instance ports under u1
      {"top.u1.l0.x", ast::ArgumentDirection::In, {0, 0}},
      {"top.u1.l0.y", ast::ArgumentDirection::Out, {0, 0}},
      {"top.u1.l1.x", ast::ArgumentDirection::In, {0, 0}},
      {"top.u1.l1.y", ast::ArgumentDirection::Out, {0, 0}},
      // leaf instance ports under u2
      {"top.u2.l0.x", ast::ArgumentDirection::In, {0, 0}},
      {"top.u2.l0.y", ast::ArgumentDirection::Out, {0, 0}},
      {"top.u2.l1.x", ast::ArgumentDirection::In, {0, 0}},
      {"top.u2.l1.y", ast::ArgumentDirection::Out, {0, 0}},
  };

  for (auto const &e : expected) {
    INFO("port path: " << e.path);
    // The single-arg lookup must resolve every hierarchical port path.
    auto *first = test.graph.lookup(e.path);
    REQUIRE(first != nullptr);
    REQUIRE(first->kind == NodeKind::Port);
    // The bounds-aware lookup returns every Port node covering the path's
    // expected range. A multi-bit output port may be split into one Port
    // node per driver, so the union of returned bounds must cover the
    // declared port width — and every returned node must be a Port with
    // the expected direction.
    auto matches = test.graph.lookup(e.path, e.bounds);
    REQUIRE(!matches.empty());
    uint64_t covered = 0;
    for (auto *n : matches) {
      REQUIRE(n->kind == NodeKind::Port);
      auto &port = n->as<Port>();
      CHECK(port.direction == e.direction);
      CHECK(std::string(port.hierarchicalPath) == e.path);
      for (uint64_t b = port.bounds.lower(); b <= port.bounds.upper(); ++b) {
        if (b >= e.bounds.lower() && b <= e.bounds.upper()) {
          covered |= (uint64_t{1} << b);
        }
      }
    }
    uint64_t expectedMask = 0;
    for (uint64_t b = e.bounds.lower(); b <= e.bounds.upper(); ++b) {
      expectedMask |= (uint64_t{1} << b);
    }
    CHECK(covered == expectedMask);
  }

  // No cross-instance leakage at the leaf level.
  CHECK(test.pathExists("top.a", "top.c"));
  CHECK(test.pathExists("top.b", "top.d"));
  CHECK_FALSE(test.pathExists("top.a", "top.d"));
  CHECK_FALSE(test.pathExists("top.b", "top.c"));
}
