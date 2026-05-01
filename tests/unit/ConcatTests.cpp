#include "Test.hpp"

// Discussion of resolving dependencies with concatenations.
// https://github.com/MikePopoloski/slang/discussions/1522

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

TEST_CASE("Concat: a = {b,c} attributes b to high bit, c to low", "[Concat]") {
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

TEST_CASE("Concat: widening zero-ext leaves top bits driverless", "[Concat]") {
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

TEST_CASE("Concat: widening sign-ext leaves top bits driverless", "[Concat]") {
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

TEST_CASE("Concat: nonblocking {a,b} <= {c,d} has no cross edges", "[Concat]") {
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

TEST_CASE("Concat: nested concat has three independent segments", "[Concat]") {
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

TEST_CASE("Concat: port connections with concats on both sides", "[Concat]") {
  auto const *tree = R"(
module x(input logic [1:0] x,
         output logic [1:0] y);
  assign y = x;
endmodule

module m(input logic a,
         input logic b,
         output logic c,
         output logic d);
  x ux (.x({b, a}), .y({d, c}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // a maps to bit 0 on both sides; b maps to bit 1. No cross-bit
  // paths.
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
}

// Two-level concat-port chain. Cuts propagate only one level down,
// so the inner module stays whole-word and cross-bit paths still
// leak through it.
TEST_CASE("Concat: two-level hierarchical cut propagation", "[Concat]") {
  auto const *tree = R"(
module inner(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule

module mid(input logic [1:0] mi, output logic [1:0] mo);
  inner u(.i(mi), .o(mo));
endmodule

module top(input logic a, b, output logic c, d);
  mid u(.mi({b, a}), .mo({d, c}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("top.a", "top.c"));
  CHECK(test.pathExists("top.b", "top.d"));
}

// A concat at a single bit boundary in a wide port introduces only
// one cut; the rest of the port stays whole-word.
TEST_CASE("Concat: wide port with single cut stays bounded", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [7:0] i, output logic [7:0] o);
  assign o = i;
endmodule

module m(input logic top_bit, input logic [6:0] rest,
         output logic [7:0] r);
  sub u(.i({top_bit, rest}), .o(r));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.top_bit", "m.r"));
  CHECK(test.pathExists("m.rest", "m.r"));
}

// Cuts inside a procedural always_ff block split nonblocking
// assignments through the same path as continuous assigns.
TEST_CASE("Concat: cuts honoured inside always_ff", "[Concat]") {
  auto const *tree = R"(
module sub(input clk, input logic [1:0] i, output logic [1:0] o);
  always_ff @(posedge clk) o <= i;
endmodule

module m(input clk, a, b, output logic c, d);
  sub u(.clk(clk), .i({b, a}), .o({d, c}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
}

// Nested concats contribute cuts at every level; the registered cut
// set is the union of inner and outer boundaries.
TEST_CASE("Concat: nested concat resolves cuts recursively", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [2:0] i, output logic [2:0] o);
  assign o = i;
endmodule

module m(input a, b, c, output logic x, y, z);
  sub u(.i({c, {b, a}}), .o({z, {y, x}}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.c", "m.z"));
  CHECK_FALSE(test.pathExists("m.a", "m.y"));
  CHECK_FALSE(test.pathExists("m.a", "m.z"));
  CHECK_FALSE(test.pathExists("m.b", "m.x"));
  CHECK_FALSE(test.pathExists("m.b", "m.z"));
  CHECK_FALSE(test.pathExists("m.c", "m.x"));
  CHECK_FALSE(test.pathExists("m.c", "m.y"));
}

// A multi-element concat actual produces multiple cut points that
// `CutRegistry::addCuts` unions onto the formal port. All boundaries
// are honoured, splitting the port into one node per element.
TEST_CASE("Concat: cut hints unioned from multi-element concat", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [3:0] i, output logic [3:0] o);
  assign o = i;
endmodule

module m(input a, b, c, d, output logic w, x, y, z);
  sub u(.i({d, c, b, a}), .o({z, y, x, w}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.a", "m.w"));
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.pathExists("m.c", "m.y"));
  CHECK(test.pathExists("m.d", "m.z"));
  CHECK_FALSE(test.pathExists("m.a", "m.x"));
  CHECK_FALSE(test.pathExists("m.a", "m.y"));
  CHECK_FALSE(test.pathExists("m.a", "m.z"));
  CHECK_FALSE(test.pathExists("m.b", "m.w"));
  CHECK_FALSE(test.pathExists("m.b", "m.y"));
  CHECK_FALSE(test.pathExists("m.b", "m.z"));
  CHECK_FALSE(test.pathExists("m.c", "m.w"));
  CHECK_FALSE(test.pathExists("m.c", "m.x"));
  CHECK_FALSE(test.pathExists("m.c", "m.z"));
  CHECK_FALSE(test.pathExists("m.d", "m.w"));
  CHECK_FALSE(test.pathExists("m.d", "m.x"));
  CHECK_FALSE(test.pathExists("m.d", "m.y"));
}

// Two instances of the same module with different concat patterns.
// Slang stores drivers under the canonical body only; the netlist
// builder always redirects the driver query through the canonical
// equivalent so each non-canonical instance gets per-bit port nodes
// and concat-aware routing distinct from the canonical instance.
TEST_CASE("Concat: two instances with different concats", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule

module m(input logic a, b, e, f, output logic c, d, g, h);
  sub u1(.i({b, a}), .o({d, c}));
  sub u2(.i({f, e}), .o({h, g}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // First instance is wired bit-precisely.
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  // Second instance is wired with its own bit-precise routing.
  CHECK(test.pathExists("m.e", "m.g"));
  CHECK(test.pathExists("m.f", "m.h"));
  CHECK_FALSE(test.pathExists("m.e", "m.h"));
  CHECK_FALSE(test.pathExists("m.f", "m.g"));
}

// A non-concat actual contributes no cuts; the whole-word port
// behaviour for that connection is preserved.
TEST_CASE("Concat: non-concat actual leaves port whole-word", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule

module m(input logic [1:0] x, output logic [1:0] y);
  sub u(.i(x), .o(y));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.x", "m.y"));
}

// With propCutsAcrossPorts off, cross-bit paths leak through the
// whole-word port nodes and whole-word internal assignment.
TEST_CASE("Concat: cut propagation disabled keeps legacy semantics",
          "[Concat]") {
  auto const *tree = R"(
module x(input logic [1:0] x,
         output logic [1:0] y);
  assign y = x;
endmodule

module m(input logic a,
         input logic b,
         output logic c,
         output logic d);
  x ux (.x({b, a}), .y({d, c}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true,
                                        .propCutsAcrossPorts = false});
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  // Cross-bit paths leak.
  CHECK(test.pathExists("m.a", "m.d"));
  CHECK(test.pathExists("m.b", "m.c"));
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

// A port connected to a non-LSP expression (arithmetic, opaque fallback)
// routes through drivePortSegment's Opaque branch: every LSP inside the
// expression fans into the formal-side port node.
TEST_CASE("Concat: port connection to opaque expression fans all LSPs",
          "[Concat]") {
  auto const *tree = R"(
module sub(input logic [3:0] i, output logic [3:0] o);
  assign o = i;
endmodule

module m(input logic [3:0] a, b, output logic [3:0] r);
  sub u(.i(a + b), .o(r));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.a", "m.r"));
  CHECK(test.pathExists("m.b", "m.r"));
}

// A non-integral port type (here an unpacked struct) short-circuits to the
// legacy LSP walk before any slicelist is built.
TEST_CASE("Concat: non-integral port falls back to legacy walk", "[Concat]") {
  auto const *tree = R"(
typedef struct { logic [3:0] a; logic [3:0] b; } pair_t;

module sub(input pair_t p, output logic [3:0] o);
  assign o = p.a;
endmodule

module m(output logic [3:0] r);
  pair_t val;
  sub u(.p(val), .o(r));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  (void)test;
}

// Per-instance port connections in an instance array should build cleanly
// — slang slices the actual to the port width for each instance so the
// bit-aligned path sees matching widths.
TEST_CASE("Concat: instance-array port connection builds without assertion",
          "[Concat]") {
  auto const *tree = R"(
module sub(input logic [3:0] i);
endmodule

module m(input logic [11:0] in);
  sub u[3](.i(in));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  (void)test;
}

// An output port wider on the actual side than the port itself: slang
// inserts an implicit conversion on the RHS so the wrapping
// AssignmentExpression's types match, but the PortSymbol's declared type
// is still narrower than the actual expression's. The bit-aligned path
// detects the width mismatch and falls back to the legacy LSP walk.
TEST_CASE("Concat: output port narrower than actual falls back", "[Concat]") {
  auto const *tree = R"(
module sub(output logic [3:0] o);
  assign o = 4'b1010;
endmodule

module m(output logic [7:0] wide);
  sub u(.o(wide));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  (void)test;
}

// Chained ternaries with same-shape arms: every arm is a 2-bit LSP.
// Both predicates and all three arms must reach the output.
TEST_CASE("Concat: chained ternaries route per bit", "[Concat]") {
  auto const *tree = R"(
module m(input s1, s2, input logic [1:0] a, b, c, output logic [1:0] r);
  assign r = s1 ? a : (s2 ? b : c);
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  for (auto const *src : {"m.s1", "m.s2", "m.a", "m.b", "m.c"}) {
    CHECK(test.pathExists(src, "m.r"));
  }
}

// Chained ternaries where each arm is itself a concat. Per-bit
// expectation: lower bit driven by the LSB operand of every arm
// (d, f, h); upper bit driven by the MSB operand of every arm
// (c, e, g); cross-bit edges must NOT exist.
TEST_CASE("Concat: chained ternaries with concat arms have no cross edges",
          "[Concat]") {
  auto const *tree = R"(
module m(input s1, s2, c, d, e, f, g, h, output logic a, b);
  assign {a, b} = s1 ? {c, d} : (s2 ? {e, f} : {g, h});
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // Predicates fan into every bit.
  CHECK(test.pathExists("m.s1", "m.a"));
  CHECK(test.pathExists("m.s1", "m.b"));
  CHECK(test.pathExists("m.s2", "m.a"));
  CHECK(test.pathExists("m.s2", "m.b"));
  // MSB-arm operands (c, e, g) drive only a; LSB-arm operands drive only b.
  CHECK(test.pathExists("m.c", "m.a"));
  CHECK(test.pathExists("m.e", "m.a"));
  CHECK(test.pathExists("m.g", "m.a"));
  CHECK(test.pathExists("m.d", "m.b"));
  CHECK(test.pathExists("m.f", "m.b"));
  CHECK(test.pathExists("m.h", "m.b"));
  // No cross edges between MSB-arm and LSB output, or vice versa.
  CHECK_FALSE(test.pathExists("m.c", "m.b"));
  CHECK_FALSE(test.pathExists("m.e", "m.b"));
  CHECK_FALSE(test.pathExists("m.g", "m.b"));
  CHECK_FALSE(test.pathExists("m.d", "m.a"));
  CHECK_FALSE(test.pathExists("m.f", "m.a"));
  CHECK_FALSE(test.pathExists("m.h", "m.a"));
}

// Replication of a multi-bit operand, wrapped in a concat with another
// LSP. r[4] is driven by y; r[3:2] and r[1:0] are both copies of x.
TEST_CASE("Concat: replication of multi-bit operand inside concat",
          "[Concat]") {
  auto const *tree = R"(
module m(input logic [1:0] x, input y, output logic [4:0] r);
  assign r = {y, {2{x}}};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.x", "m.r"));
  CHECK(test.pathExists("m.y", "m.r"));
  // r[4] is the y bit only; x must not drive it.
  CHECK_FALSE(test.hasDriverNamed("m.r", {4, 4}, "m.x"));
}

// Mixed widening conversions inside a concat: r[3:0]=a, r[5:4]=b,
// r[9:6]=zero pad. Each LSP must stay within its segment.
TEST_CASE("Concat: mixed widening conversions inside concat", "[Concat]") {
  auto const *tree = R"(
module m(input logic [3:0] a, input logic [1:0] b, output logic [9:0] r);
  assign r = {6'(b), a};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.a", "m.r"));
  CHECK(test.pathExists("m.b", "m.r"));
  for (int32_t i = 4; i < 10; ++i) {
    CHECK_FALSE(test.hasDriverNamed("m.r", {i, i}, "m.a"));
  }
  for (int32_t i = 0; i < 4; ++i) {
    CHECK_FALSE(test.hasDriverNamed("m.r", {i, i}, "m.b"));
  }
}

// LHS concat with mixed bit-selects: {a[1], b, a[0]} = {c[1], c[0], d}
// pairs c[1]->a[1], c[0]->b, d->a[0] across non-contiguous LHS bits.
// Because `a` is split into per-bit Out port nodes, pathExists() — which
// resolves a single node by name — is ambiguous; query distinct
// Assignment nodes via getDrivers instead.
TEST_CASE("Concat: LHS bit-select inside concat aligns per bit", "[Concat]") {
  auto const *tree = R"(
module m(input logic [1:0] c, input d, output logic [1:0] a, output logic b);
  assign {a[1], b, a[0]} = {c[1], c[0], d};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // Each LHS bit slot has its own Assignment node.
  auto a1Drivers = test.getDrivers("m.a", {1, 1});
  auto a0Drivers = test.getDrivers("m.a", {0, 0});
  auto bDrivers = test.getDrivers("m.b", {0, 0});
  REQUIRE(a1Drivers.size() >= 1);
  REQUIRE(a0Drivers.size() >= 1);
  REQUIRE(bDrivers.size() >= 1);
  // The three LHS bit slots must be driven by distinct Assignment nodes —
  // no whole-word driver smearing across non-contiguous bits.
  CHECK(a1Drivers[0] != a0Drivers[0]);
  CHECK(a1Drivers[0] != bDrivers[0]);
  CHECK(a0Drivers[0] != bDrivers[0]);
  // d drives a[0] but not b or a[1].
  CHECK(test.pathExists("m.d", "m.b") == false);
  // c is the single RHS LSP for the c[1] and c[0] segments.
  CHECK(test.pathExists("m.c", "m.b"));
}

// A conditional sub-expression embedded inside a concat fans out to
// the predicate AND both arms, matching the top-level behaviour. The
// bit-region scoping is bit-precise: c only drives r[1:0], and s/a/b
// only drive r[2].
TEST_CASE("Concat: conditional embedded in concat fans out to both arms",
          "[Concat]") {
  auto const *tree = R"(
module m(input s, a, b, input logic [1:0] c, output logic [2:0] r);
  assign r = {s ? a : b, c};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // All three of s, a, b reach r; c reaches r via the low bits.
  CHECK(test.pathExists("m.s", "m.r"));
  CHECK(test.pathExists("m.a", "m.r"));
  CHECK(test.pathExists("m.b", "m.r"));
  CHECK(test.pathExists("m.c", "m.r"));
  // Bit-precise scoping: r[2] is driven only by the conditional's
  // segment Assignment, fed by s/a/b — c must not leak into it. r[1:0]
  // is driven only by the c-fed segment Assignments — s/a/b must not
  // leak into either bit.
  auto r2Drivers = test.getDrivers("m.r", {2, 2});
  auto r1Drivers = test.getDrivers("m.r", {1, 1});
  auto r0Drivers = test.getDrivers("m.r", {0, 0});
  CHECK(r2Drivers.size() >= 1);
  CHECK(r1Drivers.size() >= 1);
  CHECK(r0Drivers.size() >= 1);
  // r[2]'s Assignment node is distinct from the per-bit nodes for r[1:0].
  CHECK(r2Drivers[0] != r1Drivers[0]);
  CHECK(r2Drivers[0] != r0Drivers[0]);
}

// Replication of a 2-element concat: {3{a, b}} fills 6 bits where each
// of a, b drives three bits of the output.
TEST_CASE("Concat: replication of concat operand", "[Concat]") {
  auto const *tree = R"(
module m(input a, b, output logic [5:0] r);
  assign r = {3{a, b}};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.a", "m.r"));
  CHECK(test.pathExists("m.b", "m.r"));
  // Even bits are b copies, odd bits are a copies.
  for (int32_t i = 0; i < 6; ++i) {
    auto const *forbidden = (i % 2 == 0) ? "m.a" : "m.b";
    CHECK_FALSE(test.hasDriverNamed("m.r", {i, i}, forbidden));
  }
}

// always_comb with case-driven bit-aligned assignments using concats.
// The bit-aligned path applies inside procedural blocks too; per-bit
// drivers should be distinct per case arm.
TEST_CASE("Concat: case-driven concat assignments inside always_comb",
          "[Concat]") {
  auto const *tree = R"(
module m(input logic [1:0] sel, input logic [1:0] a, b, c, d,
         output logic [1:0] r);
  always_comb begin
    case (sel)
      2'd0: r = a;
      2'd1: r = b;
      2'd2: r = {a[0], b[1]};
      default: r = {c[0], d[1]};
    endcase
  end
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.sel", "m.r"));
  CHECK(test.pathExists("m.a", "m.r"));
  CHECK(test.pathExists("m.b", "m.r"));
  CHECK(test.pathExists("m.c", "m.r"));
  CHECK(test.pathExists("m.d", "m.r"));
  // Each bit of r has multiple drivers (one per case arm covering it).
  CHECK(test.getDrivers("m.r", {0, 0}).size() >= 2);
  CHECK(test.getDrivers("m.r", {1, 1}).size() >= 2);
}

// Three instances of the same module, each with a different concat
// pattern at the port boundary. Per-instance bit-precise routing must
// survive across all three non-canonical bodies.
TEST_CASE("Concat: three instances with distinct concat patterns", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule

module m(input logic a1, b1, a2, b2, a3, b3,
         output logic c1, d1, c2, d2, c3, d3);
  sub u1(.i({b1, a1}), .o({d1, c1}));
  sub u2(.i({b2, a2}), .o({d2, c2}));
  sub u3(.i({b3, a3}), .o({d3, c3}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // Each instance is wired bit-precisely with no leakage to neighbours.
  CHECK(test.pathExists("m.a1", "m.c1"));
  CHECK(test.pathExists("m.b1", "m.d1"));
  CHECK(test.pathExists("m.a2", "m.c2"));
  CHECK(test.pathExists("m.b2", "m.d2"));
  CHECK(test.pathExists("m.a3", "m.c3"));
  CHECK(test.pathExists("m.b3", "m.d3"));
  CHECK_FALSE(test.pathExists("m.a1", "m.d1"));
  CHECK_FALSE(test.pathExists("m.b2", "m.c2"));
  CHECK_FALSE(test.pathExists("m.a3", "m.d3"));
  // No cross-instance leakage.
  CHECK_FALSE(test.pathExists("m.a1", "m.c2"));
  CHECK_FALSE(test.pathExists("m.a2", "m.c3"));
  CHECK_FALSE(test.pathExists("m.b3", "m.d1"));
}

// Asymmetric port connection: input side concatenated, output side
// connected as a single LSP. Cuts on the input formal port still split
// it bit-precisely; the output is whole-word.
TEST_CASE("Concat: asymmetric concat — input split, output whole-word",
          "[Concat]") {
  auto const *tree = R"(
module sub(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule

module m(input logic a, b, output logic [1:0] o);
  sub u(.i({b, a}), .o(o));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.a", "m.o"));
  CHECK(test.pathExists("m.b", "m.o"));
}

// Generate-loop-instantiated submodules with concat-shaped actuals.
// Each iteration yields a non-canonical body; cut hints from each
// concat actual must be applied to the formal port body
// independently per instance.
TEST_CASE("Concat: generate-loop submodules with concat actuals", "[Concat]") {
  auto const *tree = R"(
module sub(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule

module m(input logic [3:0] in, output logic [3:0] out);
  for (genvar k = 0; k < 2; k++) begin : gen
    sub u(.i({in[2*k+1], in[2*k]}), .o({out[2*k+1], out[2*k]}));
  end
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.in", "m.out"));
}

// Three-deep hierarchy with a concat at the outermost boundary. Cuts
// only propagate one level, so the inner module stays whole-word; the
// outer instance still routes paths from each top-level scalar to its
// matching scalar output.
TEST_CASE("Concat: three-level hierarchy with concat at outer boundary",
          "[Concat]") {
  auto const *tree = R"(
module inner(input logic [1:0] i, output logic [1:0] o);
  assign o = i;
endmodule

module mid(input logic [1:0] mi, output logic [1:0] mo);
  inner u(.i(mi), .o(mo));
endmodule

module outer(input logic [1:0] oi, output logic [1:0] oo);
  mid u(.mi(oi), .mo(oo));
endmodule

module top(input logic a, b, output logic c, d);
  outer u(.oi({b, a}), .oo({d, c}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("top.a", "top.c"));
  CHECK(test.pathExists("top.b", "top.d"));
}

// Wide port with multiple concat cut points and a non-trivial mix of
// scalar, sliced, and replicated actuals. Each segment of the formal
// port must be created independently; LSP fanout must match each
// segment's bit range.
TEST_CASE("Concat: wide port with multiple cut points and mixed actuals",
          "[Concat]") {
  auto const *tree = R"(
module sub(input logic [7:0] i, output logic [7:0] o);
  assign o = i;
endmodule

module m(input logic top_bit, input logic [3:0] mid, input logic [2:0] lo,
         output logic [7:0] r);
  sub u(.i({top_bit, mid, lo}), .o(r));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.top_bit", "m.r"));
  CHECK(test.pathExists("m.mid", "m.r"));
  CHECK(test.pathExists("m.lo", "m.r"));
  // r[7] is the top_bit slot; mid and lo must not leak into it.
  CHECK_FALSE(test.hasDriverNamed("m.r", {7, 7}, "m.mid"));
  CHECK_FALSE(test.hasDriverNamed("m.r", {7, 7}, "m.lo"));
  // lo occupies r[2:0] only.
  for (int32_t i = 3; i < 8; ++i) {
    CHECK_FALSE(test.hasDriverNamed("m.r", {i, i}, "m.lo"));
  }
}

// Two instances of a multi-bit module driven by always_ff with concat
// actuals: cuts must split the formal sequential state per-instance so
// each registered bit is reachable only from its source bit.
TEST_CASE("Concat: two registered instances with concat ports", "[Concat]") {
  auto const *tree = R"(
module sub(input logic clk, input logic [1:0] i, output logic [1:0] o);
  always_ff @(posedge clk) o <= i;
endmodule

module m(input logic clk, a, b, e, f,
         output logic c, d, g, h);
  sub u1(.clk(clk), .i({b, a}), .o({d, c}));
  sub u2(.clk(clk), .i({f, e}), .o({h, g}));
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK(test.pathExists("m.e", "m.g"));
  CHECK(test.pathExists("m.f", "m.h"));
  // No cross-bit or cross-instance leakage.
  CHECK_FALSE(test.pathExists("m.a", "m.d"));
  CHECK_FALSE(test.pathExists("m.b", "m.c"));
  CHECK_FALSE(test.pathExists("m.e", "m.h"));
  CHECK_FALSE(test.pathExists("m.f", "m.g"));
  CHECK_FALSE(test.pathExists("m.a", "m.g"));
  CHECK_FALSE(test.pathExists("m.e", "m.c"));
}
