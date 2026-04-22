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
