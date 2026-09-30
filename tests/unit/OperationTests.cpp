#include "Test.hpp"

#include "OperationLowering.hpp"

#include <set>
#include <string_view>

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

TEST_CASE("Operation kind names round-trip through the string helpers",
          "[Operation]") {
  auto kind = OperationKind::ArithmeticShiftRight;
  CHECK(std::string(toString(kind)) == "ArithmeticShiftRight");
  CHECK(std::string(toSymbol(kind)) == ">>>");
  CHECK(operationKindFromString("ArithmeticShiftRight") == kind);
  CHECK(!operationKindFromString("NotAnOperator").has_value());
}

TEST_CASE("Operation kind names are unique", "[Operation]") {
  // Names identify an operator in the serialised format and in reports,
  // where symbols are ambiguous, so they must not collide.
  std::set<std::string_view> names;
  for (auto i = 0U; i <= static_cast<unsigned>(OperationKind::Conditional);
       i++) {
    CHECK(names.insert(toString(static_cast<OperationKind>(i))).second);
  }
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

auto hasOperation(NetlistGraph const &graph, OperationKind kind) -> bool {
  return findOperation(graph, kind) != nullptr;
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
  REQUIRE(countOperations(test.graph) == 3);
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
  REQUIRE(countOperations(test.graph) == 3);
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

TEST_CASE("A statically dead conditional arm contributes no dependency",
          "[Operation]") {
  // Conditional arms are traversed by the flow analysis, which models
  // reachability, so an arm that constant folding deletes yields no
  // dependency in either mode.
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, input logic [7:0] mask,
         output logic [7:0] y);
  assign y = (1'b1 ? a : b) & mask;
endmodule
)";
  const NetlistTest on(tree, expandOpts());
  CHECK(findOperation(on.graph, OperationKind::Conditional) != nullptr);
  CHECK(on.pathExists("m.a", "m.y"));
  CHECK_FALSE(on.pathExists("m.b", "m.y"));

  const NetlistTest off(tree);
  CHECK(off.pathExists("m.a", "m.y"));
  CHECK_FALSE(off.pathExists("m.b", "m.y"));
}

TEST_CASE("A write in one conditional arm does not kill the other's drivers",
          "[Operation]") {
  // Both arms are only conditionally evaluated, so the definitions
  // reaching them must be rejoined rather than overwritten in sequence.
  auto const &tree = R"(
module m(input logic c, input logic [7:0] a, input logic [7:0] b,
         input logic [7:0] mask, output logic [7:0] y, output logic [7:0] o);
  logic [7:0] t;
  always_comb begin
    t = a;
    y = (c ? (t = b) : t) & mask;
    o = t;
  end
endmodule
)";
  const NetlistTest off(tree);
  CHECK(off.pathExists("m.a", "m.o"));
  CHECK(off.pathExists("m.b", "m.o"));

  const NetlistTest on(tree, expandOpts());
  CHECK(on.pathExists("m.a", "m.o"));
  CHECK(on.pathExists("m.b", "m.o"));
}

TEST_CASE("A write in a short-circuit operand does not kill prior drivers",
          "[Operation]") {
  // The right operand of `&&` runs only when the left is true, so the
  // definition reaching the operator must survive alongside it.
  auto const &tree = R"(
module m(input logic c, input logic [7:0] a, input logic [7:0] b,
         input logic [7:0] mask, output logic [7:0] y, output logic [7:0] o);
  logic [7:0] t;
  always_comb begin
    t = b;
    y = (c && (t = a)) ? mask : 8'd0;
    o = t;
  end
endmodule
)";
  const NetlistTest off(tree);
  CHECK(off.pathExists("m.a", "m.o"));
  CHECK(off.pathExists("m.b", "m.o"));

  const NetlistTest on(tree, expandOpts());
  CHECK(on.pathExists("m.a", "m.o"));
  CHECK(on.pathExists("m.b", "m.o"));
}

TEST_CASE("Expansion recovers references after a conditional operand",
          "[Operation]") {
  // The default path drops the references to the right of a conditional
  // inside an opaque expression, which is a gap in that path; expansion
  // visits every operand and records them.
  auto const &tree = R"(
module m(input logic c, input logic [7:0] a, input logic [7:0] b,
         input logic [7:0] mask, output logic [7:0] y);
  assign y = (c ? a : b) & mask;
endmodule
)";
  const NetlistTest off(tree);
  CHECK_FALSE(off.pathExists("m.mask", "m.y"));

  const NetlistTest on(tree, expandOpts());
  CHECK(on.pathExists("m.mask", "m.y"));

  // Reversing the operands avoids the gap, so both modes agree.
  auto const &reversed = R"(
module m(input logic c, input logic [7:0] a, input logic [7:0] b,
         input logic [7:0] mask, output logic [7:0] y);
  assign y = mask & (c ? a : b);
endmodule
)";
  CHECK(NetlistTest(reversed).pathExists("m.mask", "m.y"));
  CHECK(NetlistTest(reversed, expandOpts()).pathExists("m.mask", "m.y"));
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
  // Each node points at its own operator token, so chained operators
  // sharing a symbol stay distinguishable in path output.
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, input logic [7:0] c,
         output logic [7:0] y);
  assign y = a & b & c;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  REQUIRE(countOperations(test.graph) == 2);

  // Read through getLocation(), the accessor the CLI and bindings use.
  std::vector<size_t> columns;
  for (auto const &node : test.graph) {
    if (node->kind == NodeKind::Operation) {
      auto location = node->getLocation();
      REQUIRE(location.has_value());
      columns.push_back(location->column);
    }
  }
  REQUIRE(columns.size() == 2);
  CHECK(columns[0] != 0);
  CHECK(columns[1] != 0);
  CHECK(columns[0] != columns[1]);
}

TEST_CASE("Every operator kind is reachable from source", "[Operation]") {
  // Drives one of each operator through real source rather than asserting
  // the mapping tables against a copy of themselves, so a kind that no
  // expression can produce, or that maps to the wrong enumerator, shows up
  // as a missing or unexpected entry here.
  auto const &tree = R"(
module m(input logic [7:0] a, input logic [7:0] b, input logic c,
         input logic [7:0] mask,
         output logic [7:0] o_up, o_um, o_not, o_add, o_sub, o_mul, o_div,
         output logic [7:0] o_mod, o_pow, o_and, o_or, o_xor, o_xnor,
         output logic [7:0] o_shl, o_shr, o_ashl, o_ashr, o_cond,
         output logic o_lnot, o_rand, o_ror, o_rxor, o_rnand, o_rnor,
         output logic o_rxnor, o_eq, o_ne, o_ceq, o_cne, o_weq, o_wne,
         output logic o_gt, o_ge, o_lt, o_le, o_land, o_lor, o_impl, o_equiv);
  assign o_up = +a;
  assign o_um = -a;
  assign o_not = ~a;
  assign o_lnot = !a;
  assign o_rand = &a;
  assign o_ror = |a;
  assign o_rxor = ^a;
  assign o_rnand = ~&a;
  assign o_rnor = ~|a;
  assign o_rxnor = ~^a;
  assign o_add = a + b;
  assign o_sub = a - b;
  assign o_mul = a * b;
  assign o_div = a / b;
  assign o_mod = a % b;
  assign o_pow = a ** b;
  assign o_and = a & b;
  assign o_or = a | b;
  assign o_xor = a ^ b;
  assign o_xnor = a ~^ b;
  assign o_eq = a == b;
  assign o_ne = a != b;
  assign o_ceq = a === b;
  assign o_cne = a !== b;
  assign o_weq = a ==? b;
  assign o_wne = a !=? b;
  assign o_gt = a > b;
  assign o_ge = a >= b;
  assign o_lt = a < b;
  assign o_le = a <= b;
  assign o_land = a && b;
  assign o_lor = a || b;
  assign o_impl = a -> b;
  assign o_equiv = a <-> b;
  assign o_shl = a << b;
  assign o_shr = a >> b;
  assign o_ashl = a <<< b;
  assign o_ashr = a >>> b;
  assign o_cond = (c ? a : b) & mask;
endmodule
)";
  const NetlistTest test(tree, expandOpts());

  std::set<OperationKind> found;
  for (auto const &node : test.graph) {
    if (node->kind == NodeKind::Operation) {
      found.insert(node->as<Operation>().op);
    }
  }

  std::set<OperationKind> all;
  for (auto i = 0U; i <= static_cast<unsigned>(OperationKind::Conditional);
       i++) {
    all.insert(static_cast<OperationKind>(i));
  }

  // Report the shortfall by name, so a failure says which operator is
  // unreachable rather than just that two sets differ.
  for (auto kind : all) {
    CHECKED_ELSE(found.contains(kind)) {
      FAIL_CHECK("no expression produced " << toString(kind));
    }
  }
  CHECK(found.size() == all.size());
}

TEST_CASE("A non-integral operator result is left opaque", "[Operation]") {
  // Width and signedness are what an Operation records, and neither is
  // meaningful for a real, so the expression stays opaque.
  auto const &tree = R"(
module m(input real x, input real y, output real r);
  assign r = x + y;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  CHECK(countOperations(test.graph) == 0);
  CHECK(test.pathExists("m.x", "m.r"));
  CHECK(test.pathExists("m.y", "m.r"));
}

TEST_CASE("A conditional with several conditions is left opaque",
          "[Operation]") {
  // Only a single pattern-free condition is expanded, matching the
  // restriction BitSliceList applies, so the predicate is the operator's
  // only extra operand.
  auto const &tree = R"(
module m(input logic c1, input logic c2, input logic [7:0] a,
         input logic [7:0] b, input logic [7:0] mask,
         output logic [7:0] y);
  assign y = (c1 &&& c2 ? a : b) & mask;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  CHECK(findOperation(test.graph, OperationKind::Conditional) == nullptr);
  CHECK(hasOperation(test.graph, OperationKind::BitwiseAnd));
}

TEST_CASE("A conditional bearing a pattern is left opaque", "[Operation]") {
  auto const &tree = R"(
module m(input logic [7:0] v, input logic [7:0] a, input logic [7:0] b,
         input logic [7:0] mask, output logic [7:0] y);
  assign y = (v matches 8'hAA ? a : b) & mask;
endmodule
)";
  const NetlistTest test(tree, expandOpts());
  CHECK(findOperation(test.graph, OperationKind::Conditional) == nullptr);
  CHECK(hasOperation(test.graph, OperationKind::BitwiseAnd));
}
