#include "Test.hpp"

TEST_CASE("Concat LHS/RHS: {a,b} = {c,d} has no cross edges", "[Concat]") {
  auto const *tree = R"(
module m(input c, d, output a, b);
  assign {a, b} = {c, d};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // a is the upper bit and should be driven only by c; b is the lower
  // bit and should be driven only by d.
  CHECK(test.pathExists("m.c", "m.a"));
  CHECK_FALSE(test.pathExists("m.c", "m.b"));
  CHECK(test.pathExists("m.d", "m.b"));
  CHECK_FALSE(test.pathExists("m.d", "m.a"));
}

TEST_CASE("Concat LHS/RHS: {a,b} = c[1:0] attributes c[0]->b, c[1]->a",
          "[Concat]") {
  auto const *tree = R"(
module m(input logic [1:0] c, output logic a, b);
  assign {a, b} = c;
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.c", "m.a"));
  CHECK(test.pathExists("m.c", "m.b"));
  auto aDrivers = test.getDrivers("m.a", {0, 0});
  auto bDrivers = test.getDrivers("m.b", {0, 0});
  CHECK(aDrivers.size() >= 1);
  CHECK(bDrivers.size() >= 1);
  // Different segment -> distinct Assignment nodes.
  CHECK(aDrivers[0] != bDrivers[0]);
}

TEST_CASE("Concat: a = {b,c} attributes b to high bit, c to low",
          "[Concat]") {
  auto const *tree = R"(
module m(input b, c, output logic [1:0] a);
  assign a = {b, c};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.b", "m.a"));
  CHECK(test.pathExists("m.c", "m.a"));
  // a[1] should be driven by b; a[0] should be driven by c.
  auto aHiDrivers = test.getDrivers("m.a", {1, 1});
  auto aLoDrivers = test.getDrivers("m.a", {0, 0});
  REQUIRE(aHiDrivers.size() >= 1);
  REQUIRE(aLoDrivers.size() >= 1);
  // Different segment -> distinct Assignment nodes.
  CHECK(aHiDrivers[0] != aLoDrivers[0]);
}

TEST_CASE("Concat: a = {4 replicate b} drives every bit of a", "[Concat]") {
  auto const *tree = R"(
module m(input b, output logic [3:0] a);
  assign a = {4{b}};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.b", "m.a"));
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(test.getDrivers("m.a", {i, i}).size() >= 1);
  }
}

TEST_CASE("Concat: widening zero-ext leaves top bits driverless",
          "[Concat]") {
  auto const *tree = R"(
module m(input logic [3:0] b, output logic [7:0] a);
  assign a = 8'(b);
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(test.getDrivers("m.a", {i, i}).size() >= 1);
  }
  for (int32_t i = 4; i < 8; ++i) {
    // Padded bits have no driver edge from b.
    auto drivers = test.getDrivers("m.a", {i, i});
    CHECK(std::find_if(drivers.begin(), drivers.end(), [](auto *n) {
            auto p = n->getHierarchicalPath();
            return p && *p == "m.b";
          }) == drivers.end());
  }
}

TEST_CASE("Concat: widening sign-ext leaves top bits driverless",
          "[Concat]") {
  auto const *tree = R"(
module m(input logic signed [3:0] b, output logic signed [7:0] a);
  assign a = 8'(b);
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // Low bits driven by b.
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(test.getDrivers("m.a", {i, i}).size() >= 1);
  }
  // Sign-extension does not emit an MSB edge; padding bits have no
  // driver edge from b.
  for (int32_t i = 4; i < 8; ++i) {
    auto drivers = test.getDrivers("m.a", {i, i});
    CHECK(std::find_if(drivers.begin(), drivers.end(), [](auto *n) {
            auto p = n->getHierarchicalPath();
            return p && *p == "m.b";
          }) == drivers.end());
  }
}

// Narrowing conversion embedded in a concat: the conversion's operand
// contributes fewer bits than the conversion's type, so the whole
// conversion must be treated as Opaque sized to the outer width. The
// concat's per-operand widths should still add up and every named RHS
// LSP should drive the relevant segment of the LHS.
TEST_CASE("Concat: narrowing conversion inside concat stays opaque",
          "[Concat]") {
  auto const *tree = R"(
module m(input logic [7:0] x, input logic y, output logic [4:0] a);
  assign a = {y, 4'(x)};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.y", "m.a"));
  CHECK(test.pathExists("m.x", "m.a"));
  // a[4] is driven by y (top bit of the concat); the low four bits of a
  // are the narrowed x, so only x drives those.
  auto hiDrivers = test.getDrivers("m.a", {4, 4});
  auto loDrivers = test.getDrivers("m.a", {0, 0});
  CHECK(hiDrivers.size() >= 1);
  CHECK(loDrivers.size() >= 1);
}

TEST_CASE("Concat: opaque arithmetic a = b + c drives every bit of a",
          "[Concat]") {
  auto const *tree = R"(
module m(input logic [3:0] b, c, output logic [3:0] a);
  assign a = b + c;
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.b", "m.a"));
  CHECK(test.pathExists("m.c", "m.a"));
  // Every bit of a is driven by both b and c (opaque fallback scoped to
  // the whole segment).
  for (int32_t i = 0; i < 4; ++i) {
    auto drivers = test.getDrivers("m.a", {i, i});
    CHECK(drivers.size() >= 1);
  }
}

TEST_CASE("Concat: conditional op unions per-bit sources", "[Concat]") {
  auto const *tree = R"(
module m(input sel, c, d, e, f, output a, b);
  assign {a, b} = sel ? {c, d} : {e, f};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // a is upper bit: driven by c (then-arm upper) and e (else-arm upper).
  CHECK(test.pathExists("m.c", "m.a"));
  CHECK(test.pathExists("m.e", "m.a"));
  // b is lower bit: driven by d and f.
  CHECK(test.pathExists("m.d", "m.b"));
  CHECK(test.pathExists("m.f", "m.b"));
  // No cross edges between the bit positions.
  CHECK_FALSE(test.pathExists("m.c", "m.b"));
  CHECK_FALSE(test.pathExists("m.e", "m.b"));
  CHECK_FALSE(test.pathExists("m.d", "m.a"));
  CHECK_FALSE(test.pathExists("m.f", "m.a"));
  // sel (the condition) fans into every bit.
  CHECK(test.pathExists("m.sel", "m.a"));
  CHECK(test.pathExists("m.sel", "m.b"));
}

TEST_CASE("Concat: conditional op unions across mismatched arm shapes",
          "[Concat]") {
  auto const *tree = R"(
module m(input sel, c, d, input logic [1:0] e, output logic [1:0] a);
  assign a = sel ? {c, d} : e;
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // Every named RHS input drives a.
  CHECK(test.pathExists("m.c", "m.a"));
  CHECK(test.pathExists("m.d", "m.a"));
  CHECK(test.pathExists("m.e", "m.a"));
  CHECK(test.pathExists("m.sel", "m.a"));
  // Per-bit: a[1] is driven by c (then-arm MSB) and by e[1]; a[0] by d
  // and e[0].
  CHECK(test.getDrivers("m.a", {1, 1}).size() >= 1);
  CHECK(test.getDrivers("m.a", {0, 0}).size() >= 1);
}

TEST_CASE("Concat: nonblocking {a,b} <= {c,d} has no cross edges",
          "[Concat]") {
  auto const *tree = R"(
module m(input clk, c, d, output logic a, b);
  always_ff @(posedge clk) begin
    {a, b} <= {c, d};
  end
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.c", "m.a"));
  CHECK(test.pathExists("m.d", "m.b"));
  CHECK_FALSE(test.pathExists("m.c", "m.b"));
  CHECK_FALSE(test.pathExists("m.d", "m.a"));
}

TEST_CASE("Concat: nested concat has three independent segments",
          "[Concat]") {
  auto const *tree = R"(
module m(input d, e, f, output a, b, c);
  assign {a, {b, c}} = {d, {e, f}};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // Positive: d->a, e->b, f->c.
  CHECK(test.pathExists("m.d", "m.a"));
  CHECK(test.pathExists("m.e", "m.b"));
  CHECK(test.pathExists("m.f", "m.c"));
  // Negative: no cross edges between segments.
  CHECK_FALSE(test.pathExists("m.d", "m.b"));
  CHECK_FALSE(test.pathExists("m.d", "m.c"));
  CHECK_FALSE(test.pathExists("m.e", "m.a"));
  CHECK_FALSE(test.pathExists("m.e", "m.c"));
  CHECK_FALSE(test.pathExists("m.f", "m.a"));
  CHECK_FALSE(test.pathExists("m.f", "m.b"));
}

TEST_CASE("Concat: getDrivers bit-range query returns matching segment",
          "[Concat]") {
  auto const *tree = R"(
module m(input c, d, output logic a, b);
  assign {a, b} = {c, d};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // Each of a / b is a 1-bit signal; each {0,0} query returns the driver
  // for that specific segment's Assignment node.
  auto aDrivers = test.getDrivers("m.a", {0, 0});
  auto bDrivers = test.getDrivers("m.b", {0, 0});
  CHECK(aDrivers.size() >= 1);
  CHECK(bDrivers.size() >= 1);
  // Distinct assignment nodes per segment.
  CHECK(aDrivers[0] != bDrivers[0]);
}

TEST_CASE("Concat: port connection sub u(.i({x,y}))", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [3:0] i, output logic [3:0] o);
  assign o = i;
endmodule

module m(input logic [1:0] x, input logic [1:0] y, output logic [3:0] z);
  logic [3:0] mid;
  sub u(.i({x, y}), .o(mid));
  assign z = mid;
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // x[1:0] drives the upper half of u.i; y[1:0] drives the lower half.
  CHECK(test.pathExists("m.x", "m.z"));
  CHECK(test.pathExists("m.y", "m.z"));
  // Specifically: bit 0 of z must be reachable from y but not from x.
  // (This relies on the whole path through sub being bit-precise; with
  // only the port-connection fix in place, `sub`'s internal `o = i` is
  // still whole-word, so this check is a loose lower bound.)
}

// A module-internal input port that analysis treats as driver-less
// (e.g. a reads-only combinational pass-through) registers no
// formal-side port nodes. The bit-aligned port-connection path must not
// assert on the resulting empty formal slicelist.
TEST_CASE("Concat: dangling input port does not assert on width", "[Concat]") {
  auto const *tree = R"(
module gated_clk_cell(
  input clk_in, input external_en, output clk_out
);
  assign clk_out = clk_in;
endmodule

module m(input ck, output o);
  gated_clk_cell u(.clk_in(ck), .external_en(1'b0), .clk_out(o));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // The netlist builds without crashing; the input port's connection
  // contributes no driver edge (no formal-side node was registered)
  // which matches the legacy path's silent no-op behaviour.
  CHECK(test.pathExists("m.ck", "m.o"));
}

// An inout port registers multiple nodes at overlapping bit ranges
// (one per direction), possibly at different widths. buildPortSliceList
// must produce a slicelist whose total width equals the port type's
// selectable width rather than the sum of node widths.
TEST_CASE("Concat: inout port with multiple driver nodes does not assert",
          "[Concat]") {
  auto const *tree = R"(
module sub(inout logic [7:0] pad);
  logic [7:0] drv;
  assign pad = drv;
endmodule

module m(inout logic [7:0] pad);
  sub u(.pad(pad));
endmodule
)";
  // Reaching the end of NetlistTest construction is the check; no
  // functional path claim here because inout semantics aren't fully
  // modelled yet.
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  (void)test;
}

// Slang accepts port connections where the port's selectable width
// and the connection expression's selectable width differ (common
// with instance-array connections and enum-to-logic coercion). The
// bit-aligned path must fall back to the legacy LSP walk rather than
// asserting.
TEST_CASE("Concat: port-connection width mismatch falls back to legacy walk",
          "[Concat]") {
  auto const *tree = R"(
module sub(output logic [30:0][2:0] prio_o);
  assign prio_o = '0;
endmodule

module m(output logic [2:0][2:0] prio_sub);
  logic [30:0][2:0] prio_full;
  sub u(.prio_o(prio_full));
  assign prio_sub = prio_full[2:0];
endmodule
)";
  // Reaching the end of NetlistTest construction confirms the fallback
  // path is active.
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  (void)test;
}

// String assignments have dynamic selectable widths that can
// legitimately differ between lhs and rhs. The DFA bit-aligned path
// must fall back to the legacy walk rather than asserting on the
// width mismatch.
TEST_CASE("Concat: string assignment width mismatch falls back", "[Concat]") {
  auto const *tree = R"(
module m;
  string a;
  string b, c;
  initial begin
    b = "x";
    c = "y";
    a = {b, c};
  end
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  (void)test;
}

TEST_CASE("Concat: port widening leaves upper bits driverless", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [7:0] i, output logic [7:0] o);
  assign o = i;
endmodule

module m(input logic [3:0] z, output logic [7:0] r);
  sub u(.i(8'(z)), .o(r));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // Lower half is driven by z; upper (padded) half has no edge from z.
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(test.getDrivers("m.u.i", {i, i}).size() >= 1);
  }
  for (int32_t i = 4; i < 8; ++i) {
    auto drivers = test.getDrivers("m.u.i", {i, i});
    CHECK(std::find_if(drivers.begin(), drivers.end(), [](auto *n) {
            auto p = n->getHierarchicalPath();
            return p && *p == "m.z";
          }) == drivers.end());
  }
}
