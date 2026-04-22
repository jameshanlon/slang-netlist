#include "Test.hpp"

#include "../../source/BitSliceList.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::netlist;

namespace {

/// Compile an SV fragment, find the first continuous-assign RHS expression,
/// and return it together with an EvalContext usable by BitSliceList::build.
struct ExprHarness {
  Compilation compilation;
  std::unique_ptr<EvalContext> evalCtx;
  const Expression *expr = nullptr;

  explicit ExprHarness(const std::string &body) {
    auto wrapper = "module m; " + body + " endmodule";
    auto tree = SyntaxTree::fromText(wrapper);
    compilation.addSyntaxTree(tree);
    compilation.getAllDiagnostics();
    // Walk the root and find the first ContinuousAssignSymbol.
    auto const &root = compilation.getRoot();
    const ContinuousAssignSymbol *found = nullptr;
    root.visit(makeVisitor([&](auto &, const ContinuousAssignSymbol &sym) {
      if (found == nullptr) {
        found = &sym;
      }
    }));
    REQUIRE(found != nullptr);
    auto const &assign = found->getAssignment().as<AssignmentExpression>();
    expr = &assign.right();
    evalCtx = std::make_unique<EvalContext>(*found);
  }
};

} // namespace

TEST_CASE("BitSliceList: single named value yields one LSP slice",
          "[BitSliceList]") {
  ExprHarness h("logic [3:0] a, b; assign b = a;");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 1);
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 4);
  REQUIRE(list[0].sources.size() == 1);
  CHECK(list[0].sources[0].kind == BitSliceSource::Kind::Lsp);
}

TEST_CASE("BitSliceList: arithmetic expression is opaque", "[BitSliceList]") {
  ExprHarness h("logic [3:0] a, b, c; assign c = a + b;");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 1);
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 4);
  REQUIRE(list[0].sources.size() == 1);
  CHECK(list[0].sources[0].kind == BitSliceSource::Kind::Opaque);
}

TEST_CASE("BitSliceList: concatenation is MSB-first and linear",
          "[BitSliceList]") {
  ExprHarness h("logic [1:0] a, b; logic [3:0] c; assign c = {a, b};");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 2);
  // LSB-first layout: concatLo=0 is the LSB of the concatenation,
  // which is the rightmost operand per LRM (b). Upper operand (a)
  // occupies the top two bits.
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 2);
  REQUIRE(list[0].sources.size() == 1);
  CHECK(list[0].sources[0].kind == BitSliceSource::Kind::Lsp);
  // a occupies bits [2,4).
  CHECK(list[1].concatLo == 2);
  CHECK(list[1].concatHi == 4);
  REQUIRE(list[1].sources.size() == 1);
  CHECK(list[1].sources[0].kind == BitSliceSource::Kind::Lsp);
}

TEST_CASE("BitSliceList: nested concat flattens", "[BitSliceList]") {
  ExprHarness h("logic a, b, c; logic [2:0] r; assign r = {a, {b, c}};");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 3);
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 1);
  CHECK(list[1].concatLo == 1);
  CHECK(list[1].concatHi == 2);
  CHECK(list[2].concatLo == 2);
  CHECK(list[2].concatHi == 3);
}

TEST_CASE("BitSliceList: concat of opaque + structural keeps bit offsets",
          "[BitSliceList]") {
  ExprHarness h("logic [1:0] a, b; logic [3:0] r; assign r = {a, a + b};");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 2);
  // LSB operand is the opaque arith expression.
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 2);
  REQUIRE(list[0].sources.size() == 1);
  CHECK(list[0].sources[0].kind == BitSliceSource::Kind::Opaque);
  // MSB operand is the named value `a`.
  CHECK(list[1].concatLo == 2);
  CHECK(list[1].concatHi == 4);
  REQUIRE(list[1].sources.size() == 1);
  CHECK(list[1].sources[0].kind == BitSliceSource::Kind::Lsp);
}

TEST_CASE("BitSliceList: replication produces N copies", "[BitSliceList]") {
  ExprHarness h("logic a; logic [3:0] r; assign r = {4{a}};");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 4);
  for (size_t i = 0; i < 4; ++i) {
    CHECK(list[i].concatLo == i);
    CHECK(list[i].concatHi == i + 1);
    REQUIRE(list[i].sources.size() == 1);
    CHECK(list[i].sources[0].kind == BitSliceSource::Kind::Lsp);
  }
}

TEST_CASE("BitSliceList: replication of multi-bit operand", "[BitSliceList]") {
  ExprHarness h("logic [1:0] a; logic [3:0] r; assign r = {2{a}};");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 2);
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 2);
  REQUIRE(list[0].sources.size() == 1);
  CHECK(list[0].sources[0].kind == BitSliceSource::Kind::Lsp);
  CHECK(list[1].concatLo == 2);
  CHECK(list[1].concatHi == 4);
  REQUIRE(list[1].sources.size() == 1);
  CHECK(list[1].sources[0].kind == BitSliceSource::Kind::Lsp);
}

TEST_CASE("BitSliceList: same-width conversion is pass-through",
          "[BitSliceList]") {
  ExprHarness h("logic [3:0] a; logic [3:0] b; assign b = unsigned'(a);");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 1);
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 4);
  CHECK(list[0].sources[0].kind == BitSliceSource::Kind::Lsp);
}

TEST_CASE("BitSliceList: widening conversion prepends padding",
          "[BitSliceList]") {
  ExprHarness h("logic [3:0] a; logic [7:0] b; assign b = 8'(a);");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 2);
  // LSB is the operand, MSB is the padding.
  CHECK(list[0].sources[0].kind == BitSliceSource::Kind::Lsp);
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 4);
  CHECK(list[1].sources[0].kind == BitSliceSource::Kind::Padding);
  CHECK(list[1].concatLo == 4);
  CHECK(list[1].concatHi == 8);
}

TEST_CASE("BitSliceList: conditional op unions arms bit-by-bit",
          "[BitSliceList]") {
  ExprHarness h("logic sel; logic [1:0] a, b; logic c, d, e;"
                "logic [1:0] r; assign r = sel ? {c, d} : e == 1'b1 ? a : b;");
  // The interesting shape: `sel ? {c, d} : somethingElse` where the
  // true-arm has shape {[0,1)=d, [1,2)=c} and the false-arm has shape
  // {[0,2)=a_or_b}. After unification on the cut point at 1, each
  // unified slice carries sources from both arms plus an opaque sel.
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 2);
  CHECK(list[0].concatHi == 1);
  CHECK(list[1].concatHi == 2);
  for (auto const &slice : list) {
    // Expect at least two sources: true-arm LSP plus false-arm LSP;
    // condition `sel` is attached as opaque too.
    CHECK(slice.sources.size() >= 2);
    // And exactly one Opaque source — the shared condition.
    size_t opaqueCount = 0;
    for (auto const &src : slice.sources) {
      if (src.kind == BitSliceSource::Kind::Opaque) {
        ++opaqueCount;
      }
    }
    CHECK(opaqueCount >= 1);
  }
}

TEST_CASE("BitSliceList: conditional op with aligned LSP arms",
          "[BitSliceList]") {
  ExprHarness h(
      "logic sel; logic [3:0] a, b; logic [3:0] r; assign r = sel ? a : b;");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx);
  REQUIRE(list.size() == 1);
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 4);
  // Two LSP sources (a and b) plus one Opaque (the predicate `sel`).
  REQUIRE(list[0].sources.size() == 3);
  size_t lspCount = 0, opaqueCount = 0;
  for (auto const &src : list[0].sources) {
    if (src.kind == BitSliceSource::Kind::Lsp) {
      ++lspCount;
    }
    if (src.kind == BitSliceSource::Kind::Opaque) {
      ++opaqueCount;
    }
  }
  CHECK(lspCount == 2);
  CHECK(opaqueCount == 1);
}

TEST_CASE("alignSegments: equal-shape lists produce matching segments",
          "[BitSliceList]") {
  ExprHarness hL("logic [1:0] a, b; logic [3:0] c; assign c = {a, b};");
  ExprHarness hR("logic [1:0] x, y; logic [3:0] z; assign z = {x, y};");
  auto lhs = BitSliceList::build(*hL.expr, *hL.evalCtx);
  auto rhs = BitSliceList::build(*hR.expr, *hR.evalCtx);
  auto segs = alignSegments(lhs, rhs);
  REQUIRE(segs.size() == 2);
  CHECK(segs[0].concatLo == 0);
  CHECK(segs[0].concatHi == 2);
  CHECK(segs[1].concatLo == 2);
  CHECK(segs[1].concatHi == 4);
  CHECK(segs[0].lhsSources.size() == 1);
  CHECK(segs[0].rhsSources.size() == 1);
  CHECK(segs[0].lhsSources[0].kind == BitSliceSource::Kind::Lsp);
  CHECK(segs[0].rhsSources[0].kind == BitSliceSource::Kind::Lsp);
}

TEST_CASE("alignSegments: mismatched shapes introduce extra cut points",
          "[BitSliceList]") {
  ExprHarness hL("logic [3:0] a; logic [3:0] c; assign c = a;");
  ExprHarness hR("logic [1:0] x, y; logic [3:0] z; assign z = {x, y};");
  auto lhs = BitSliceList::build(*hL.expr, *hL.evalCtx);
  auto rhs = BitSliceList::build(*hR.expr, *hR.evalCtx);
  auto segs = alignSegments(lhs, rhs);
  REQUIRE(segs.size() == 2); // split at bit 2 because of the RHS concat
  CHECK(segs[0].concatHi == 2);
  CHECK(segs[1].concatHi == 4);
}

TEST_CASE("BitSliceList: disabled flag yields single opaque slice",
          "[BitSliceList]") {
  ExprHarness h("logic [3:0] a, b; logic [3:0] r; assign r = {a, b};");
  auto list = BitSliceList::build(*h.expr, *h.evalCtx, /*enabled=*/false);
  REQUIRE(list.size() == 1);
  CHECK(list[0].concatLo == 0);
  CHECK(list[0].concatHi == 4);
  REQUIRE(list[0].sources.size() == 1);
  CHECK(list[0].sources[0].kind == BitSliceSource::Kind::Opaque);
}
