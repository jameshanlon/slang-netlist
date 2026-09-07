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

auto countOperations(NetlistGraph const &graph) -> size_t {
  size_t count = 0;
  for (auto const &node : graph) {
    if (node->kind == NodeKind::Operation) {
      ++count;
    }
  }
  return count;
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

TEST_CASE("Constant driver: constant-folded operator expression still reports "
          "its constants",
          "[Constant]") {
  auto const &tree = R"(
module m(output logic [3:0] y);
  assign y = 4'd5 & 4'd3;
endmodule
)";
  BuilderOptions opts;
  opts.parallel = false;
  opts.expandOperations = true;
  const NetlistTest test(tree, opts);

  // slang constant-folds this expression before the netlist sees it, so
  // no Operation node is created.
  CHECK(countOperations(test.graph) == 0);

  auto *sink = test.graph.lookup("m.y");
  REQUIRE(sink != nullptr);
  auto constants = test.graph.getConstantDrivers(*sink);
  CHECK_FALSE(constants.empty());
}

TEST_CASE("Constant driver: an input in the fan-in defeats constant drive",
          "[Constant]") {
  auto const &tree = R"(
module m(input logic [3:0] a, output logic [3:0] y);
  assign y = a & 4'd3;
endmodule
)";
  BuilderOptions opts;
  opts.parallel = false;
  opts.expandOperations = true;
  const NetlistTest test(tree, opts);

  // The non-foldable `&` produces an Operation node in the fan-in.
  CHECK(countOperations(test.graph) >= 1);

  auto *sink = test.graph.lookup("m.y");
  REQUIRE(sink != nullptr);
  CHECK(test.graph.getConstantDrivers(*sink).empty());
}

TEST_CASE("Constant driver: constants reach a sink only through an "
          "Operation node",
          "[Constant]") {
  auto const &tree = R"(
module m(output logic [3:0] y);
  logic [3:0] a;
  assign a = 4'd5;
  assign y = a & 4'd3;
endmodule
)";
  BuilderOptions opts;
  opts.parallel = false;
  opts.expandOperations = true;
  const NetlistTest test(tree, opts);

  // Splitting across two continuous assignments defeats slang's
  // constant folder, so the non-foldable `&` produces a real
  // Operation node in the fan-in.
  REQUIRE(countOperations(test.graph) >= 1);

  auto *sink = test.graph.lookup("m.y");
  REQUIRE(sink != nullptr);
  auto constants = test.graph.getConstantDrivers(*sink);
  REQUIRE_FALSE(constants.empty());
  for (auto *n : constants) {
    CHECK(n->kind == NodeKind::Constant);
  }

  // No Port or State node in the fan-in (other than the sink itself,
  // which getConstantDrivers skips), so the result can only be
  // non-empty because Operation is a pass-through.
  auto fanIn = test.graph.getCombFanIn(*sink);
  for (auto *n : fanIn) {
    if (n == sink) {
      continue;
    }
    CHECK(n->kind != NodeKind::Port);
    CHECK(n->kind != NodeKind::State);
  }
}
