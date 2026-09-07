#include "Test.hpp"

#include "OperationLowering.hpp"

TEST_CASE("Binary operators map onto their netlist counterparts",
          "[Operation]") {
  CHECK(mapBinaryOperator(ast::BinaryOperator::Add) == OperationKind::Add);
  CHECK(mapBinaryOperator(ast::BinaryOperator::BinaryAnd) ==
        OperationKind::BitwiseAnd);
  CHECK(mapBinaryOperator(ast::BinaryOperator::LogicalAnd) ==
        OperationKind::LogicalAnd);
  CHECK(mapBinaryOperator(ast::BinaryOperator::ArithmeticShiftRight) ==
        OperationKind::ArithmeticShiftRight);
  CHECK(mapBinaryOperator(ast::BinaryOperator::WildcardEquality) ==
        OperationKind::WildcardEquality);
}

TEST_CASE("Unary operators map onto their netlist counterparts",
          "[Operation]") {
  CHECK(mapUnaryOperator(ast::UnaryOperator::Minus) ==
        OperationKind::UnaryMinus);
  CHECK(mapUnaryOperator(ast::UnaryOperator::BitwiseNot) ==
        OperationKind::BitwiseNot);
  CHECK(mapUnaryOperator(ast::UnaryOperator::BitwiseAnd) ==
        OperationKind::ReductionAnd);
  CHECK(mapUnaryOperator(ast::UnaryOperator::LogicalNot) ==
        OperationKind::LogicalNot);
}

TEST_CASE("Increment and decrement operators are not expanded", "[Operation]") {
  CHECK(!mapUnaryOperator(ast::UnaryOperator::Preincrement).has_value());
  CHECK(!mapUnaryOperator(ast::UnaryOperator::Predecrement).has_value());
  CHECK(!mapUnaryOperator(ast::UnaryOperator::Postincrement).has_value());
  CHECK(!mapUnaryOperator(ast::UnaryOperator::Postdecrement).has_value());
}

namespace {

auto expandOpts() -> BuilderOptions {
  BuilderOptions opts;
  opts.parallel = false;
  opts.expandOperations = true;
  return opts;
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

auto hasOperation(NetlistGraph const &graph, OperationKind kind) -> bool {
  for (auto const &node : graph) {
    if (node->kind == NodeKind::Operation && node->as<Operation>().op == kind) {
      return true;
    }
  }
  return false;
}

auto findOperation(NetlistGraph const &graph, OperationKind kind)
    -> Operation const * {
  for (auto const &node : graph) {
    if (node->kind == NodeKind::Operation && node->as<Operation>().op == kind) {
      return &node->as<Operation>();
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("Bitwise binary operator becomes an Operation node", "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, output logic [7:0] y);
  assign y = a & b;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  REQUIRE(countOperations(test.graph) == 1);
  auto const *op = findOperation(test.graph, OperationKind::BitwiseAnd);
  REQUIRE(op != nullptr);
  CHECK(op->width == 8);
  CHECK_FALSE(op->isSigned);
  // Two operands in, one Assignment out.
  CHECK(op->inDegree() == 2);
  CHECK(op->outDegree() == 1);
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Arithmetic, relational and shift operators are expanded",
          "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b,
         output logic [7:0] s, output logic lt, output logic [7:0] sh);
  assign s = a + b;
  assign lt = a < b;
  assign sh = a << 2;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  CHECK(hasOperation(test.graph, OperationKind::Add));
  CHECK(hasOperation(test.graph, OperationKind::LessThan));
  CHECK(hasOperation(test.graph, OperationKind::LogicalShiftLeft));
}

TEST_CASE("Unary and reduction operators are expanded", "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] n,
         output logic r, output logic l);
  assign n = ~a;
  assign r = ^a;
  assign l = !a;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  CHECK(hasOperation(test.graph, OperationKind::BitwiseNot));
  CHECK(hasOperation(test.graph, OperationKind::ReductionXor));
  CHECK(hasOperation(test.graph, OperationKind::LogicalNot));
}

TEST_CASE("Conditional operator becomes an Operation node", "[Operation]") {
  // A top-level `?:` with integral arms is decomposed bit-wise before
  // lowering sees it, so reach it through an enclosing opaque operator.
  auto const &tree = R"(
module m(input logic c, input logic [7:0] a, input logic [3:0] b,
         input logic [7:0] mask, output logic [7:0] y);
  assign y = (c ? a : b) & mask;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  auto const *op = findOperation(test.graph, OperationKind::Conditional);
  REQUIRE(op != nullptr);
  CHECK(op->width == 8);
  // Condition plus both arms.
  CHECK(op->inDegree() == 3);
  // Feeds the enclosing operator.
  CHECK(op->outDegree() == 1);
  CHECK(test.pathExists("m.c", "m.y"));
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Nested operators produce a chain of Operation nodes",
          "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, input logic [7:0] c,
         output logic [7:0] y);
  assign y = (a & b) | c;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  REQUIRE(countOperations(test.graph) == 2);
  auto const *andOp = findOperation(test.graph, OperationKind::BitwiseAnd);
  auto const *orOp = findOperation(test.graph, OperationKind::BitwiseOr);
  REQUIRE(andOp != nullptr);
  REQUIRE(orOp != nullptr);
  // The AND feeds the OR, which feeds the Assignment.
  CHECK(andOp->outDegree() == 1);
  CHECK(andOp->inDegree() == 2);
  CHECK(orOp->inDegree() == 2);
  CHECK(orOp->outDegree() == 1);
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.c", "m.y"));
}

TEST_CASE("An opaque operand under an operator still records its inputs",
          "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, output logic [7:0] y);
  function automatic logic [7:0] f(input logic [7:0] x);
    return x;
  endfunction
  assign y = f(a) & b;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  REQUIRE(countOperations(test.graph) == 1);
  CHECK(hasOperation(test.graph, OperationKind::BitwiseAnd));
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Increment operators are left opaque", "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] y);
  always_comb begin
    automatic logic [7:0] t = a;
    y = t++;
  end
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  CHECK(countOperations(test.graph) == 0);
}

TEST_CASE("Operator expansion is off by default", "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, input logic [7:0] c,
         output logic [7:0] y);
  assign y = (a & b) | c;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(countOperations(test.graph) == 0);
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.c", "m.y"));
}

TEST_CASE("Expansion adds nodes but preserves reachability", "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, input logic [7:0] c,
         output logic [7:0] y);
  assign y = (a & b) | c;
endmodule
)";
  const NetlistTest off(tree);
  const NetlistTest on(tree, expandOpts());
  CHECK(on.graph.numNodes() > off.graph.numNodes());
  for (auto const *name : {"m.a", "m.b", "m.c"}) {
    CHECK(off.pathExists(name, "m.y"));
    CHECK(on.pathExists(name, "m.y"));
  }
}

TEST_CASE("Legacy assignment path expands operators too", "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, output logic [7:0] y);
  assign y = a & b;
endmodule
)";
  auto opts = expandOpts();
  opts.resolveAssignBits = false;
  const NetlistTest test(tree, opts);
  REQUIRE(countOperations(test.graph) == 1);
  CHECK(hasOperation(test.graph, OperationKind::BitwiseAnd));
  CHECK(test.pathExists("m.a", "m.y"));
}
