#include "Test.hpp"

namespace {

auto countConstants(NetlistGraph const &graph) -> size_t {
  size_t count = 0;
  for (auto const &node : graph) {
    if (node->kind == NodeKind::Constant) {
      ++count;
    }
  }
  return count;
}

auto firstConstant(NetlistGraph const &graph) -> Constant const * {
  for (auto const &node : graph) {
    if (node->kind == NodeKind::Constant) {
      return &node->as<Constant>();
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("Constant driver: pure-literal continuous assignment", "[Constant]") {
  auto const &tree = R"(
module m(output logic [3:0] x);
  assign x = 4'd5;
endmodule
)";
  const NetlistTest test(tree);
  REQUIRE(countConstants(test.graph) == 1);
  auto const *c = firstConstant(test.graph);
  REQUIRE(c != nullptr);
  CHECK(c->width == 4);
  REQUIRE(c->value.isInteger());
  CHECK(c->value.integer() == 5);
  CHECK(c->outDegree() == 1);
}

TEST_CASE("Constant driver: zero-extension produces a constant-zero source",
          "[Constant]") {
  auto const &tree = R"(
module m(input logic [3:0] a, output logic [7:0] b);
  assign b = 8'(a);
endmodule
)";
  const NetlistTest test(tree);
  REQUIRE(countConstants(test.graph) == 1);
  auto const *c = firstConstant(test.graph);
  REQUIRE(c != nullptr);
  CHECK(c->width == 4);
  REQUIRE(c->value.isInteger());
  CHECK(c->value.integer() == 0);
}

TEST_CASE("Constant driver: signed widening keeps padding (no constant)",
          "[Constant]") {
  auto const &tree = R"(
module m(input logic signed [3:0] a, output logic signed [7:0] b);
  assign b = 8'(a);
endmodule
)";
  const NetlistTest test(tree);
  CHECK(countConstants(test.graph) == 0);
}

TEST_CASE("Constant driver: literal port connection", "[Constant]") {
  auto const &tree = R"(
module sub(input logic [2:0] in, output logic [2:0] out);
  assign out = in;
endmodule
module m(output logic [2:0] o);
  sub u(.in(3'd5), .out(o));
endmodule
)";
  const NetlistTest test(tree);
  REQUIRE(countConstants(test.graph) == 1);
  auto const *c = firstConstant(test.graph);
  REQUIRE(c != nullptr);
  CHECK(c->width == 3);
  REQUIRE(c->value.isInteger());
  CHECK(c->value.integer() == 5);
}

TEST_CASE("Constant driver: ternary arm with a constant", "[Constant]") {
  auto const &tree = R"(
module m(input logic sel, input logic [3:0] a, output logic [3:0] b);
  assign b = sel ? a : 4'd0;
endmodule
)";
  const NetlistTest test(tree);
  // The conditional unifies into a single full-width segment containing
  // an LSP source for `a`, the Opaque condition `sel`, and a Constant
  // source for the false arm — so exactly one Constant node is emitted.
  REQUIRE(countConstants(test.graph) == 1);
  auto const *c = firstConstant(test.graph);
  REQUIRE(c != nullptr);
  CHECK(c->width == 4);
  REQUIRE(c->value.isInteger());
  CHECK(c->value.integer() == 0);
}

TEST_CASE("Constant driver: procedural assignment of literal", "[Constant]") {
  auto const &tree = R"(
module m(input logic clk, input logic rst, output logic q);
  always_ff @(posedge clk)
    if (rst)
      q <= 1'b0;
    else
      q <= 1'b1;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(countConstants(test.graph) == 2);
}
