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

auto findNodeOfKind(NetlistGraph const &graph, NodeKind kind)
    -> NetlistNode const * {
  for (auto const &node : graph) {
    if (node->kind == kind) {
      return node.get();
    }
  }
  return nullptr;
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

TEST_CASE("Operators lower inside a procedural conditional", "[Operation]") {
  auto const &tree = R"(
module m(input logic c, input logic [7:0] a, input logic [7:0] b,
         output logic [7:0] y);
  always_comb begin
    y = 0;
    if (c) y = a & b;
  end
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  auto const *op = findOperation(test.graph, OperationKind::BitwiseAnd);
  auto const *cond = findNodeOfKind(test.graph, NodeKind::Conditional);
  REQUIRE(op != nullptr);
  REQUIRE(cond != nullptr);

  // Both operands feed the operator, which drives the guarded assignment.
  CHECK(op->inDegree() == 2);
  REQUIRE(op->outDegree() == 1);
  auto const &assign = (*op->begin())->getTargetNode();
  CHECK(assign.kind == NodeKind::Assignment);

  // The branch condition guards the assignment, not the operator: an
  // operand of an expression does not depend on the enclosing branch.
  CHECK(cond->findEdgeTo(assign) != cond->end());
  CHECK(cond->findEdgeTo(*op) == cond->end());

  CHECK(test.pathExists("m.c", "m.y"));
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("A statically dead conditional arm still contributes a dependency",
          "[Operation]") {
  // Deliberate, sound over-approximation: lowering recurses into both arms
  // of a conditional without modelling reachability, so an arm that
  // constant folding would delete still yields a dependency. Erring
  // towards extra edges matches how opaque expressions are handled
  // elsewhere. The default path defers to slang's flow analysis, which
  // does drop the dead arm, so the two modes diverge here by design.
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, input logic [7:0] mask,
         output logic [7:0] y);
  assign y = (1'b1 ? a : b) & mask;
endmodule
)";
  const NetlistTest on(tree, expandOpts());
  CHECK(findOperation(on.graph, OperationKind::Conditional) != nullptr);
  CHECK(on.pathExists("m.a", "m.y"));
  CHECK(on.pathExists("m.b", "m.y"));

  const NetlistTest off(tree);
  CHECK(off.pathExists("m.a", "m.y"));
  CHECK_FALSE(off.pathExists("m.b", "m.y"));
}

TEST_CASE("An operator spanning several aligned segments is duplicated",
          "[Operation]") {
  // The opaque source is lowered once per aligned segment of the target,
  // so a two-segment concatenation yields one operator node per segment.
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b,
         output logic [3:0] p, output logic [3:0] q);
  assign {p, q} = a & b;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  CHECK(countOperations(test.graph) == 2);
  CHECK(test.pathExists("m.a", "m.p"));
  CHECK(test.pathExists("m.a", "m.q"));
  CHECK(test.pathExists("m.b", "m.p"));
  CHECK(test.pathExists("m.b", "m.q"));
}

TEST_CASE("An assignment inside an operand does not leak to its siblings",
          "[Operation]") {
  // The output argument retargets the current node while the first operand
  // is visited; the second operand must still attach to the operator.
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, output logic [7:0] y,
         output logic [7:0] o);
  function automatic logic [7:0] g(input logic [7:0] x, output logic [7:0] w);
    w = x;
    return x;
  endfunction
  always_comb begin
    o = 0;
    y = g(a, o) & b;
  end
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  auto const *op = findOperation(test.graph, OperationKind::BitwiseAnd);
  REQUIRE(op != nullptr);
  CHECK(op->inDegree() == 2);
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Chained same-symbol operators get distinct locations",
          "[Operation]") {
  // Both nodes previously took the location of their whole subexpression,
  // so two chained `&`s were indistinguishable in path output. Each must
  // now point at its own operator token.
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, input logic [7:0] c,
         output logic [7:0] y);
  assign y = a & b & c;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  REQUIRE(countOperations(test.graph) == 2);

  std::vector<size_t> columns;
  for (auto const &node : test.graph) {
    if (node->kind == NodeKind::Operation) {
      columns.push_back(node->as<Operation>().location.column);
    }
  }
  REQUIRE(columns.size() == 2);
  CHECK(columns[0] != 0);
  CHECK(columns[1] != 0);
  CHECK(columns[0] != columns[1]);
}
