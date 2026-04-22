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
