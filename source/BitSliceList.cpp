#include "BitSliceList.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/types/Type.h"

#include <cassert>

using namespace slang;
using namespace slang::ast;

namespace slang::netlist {

namespace {

/// Return the selectable bit width of @p expr's type.
auto exprWidth(const Expression &expr) -> uint64_t {
  return expr.type->getSelectableWidth();
}

void buildInto(BitSliceList &out, const Expression &expr,
               EvalContext &evalCtx) {
  switch (expr.kind) {
  case ExpressionKind::NamedValue:
  case ExpressionKind::HierarchicalValue:
  case ExpressionKind::ElementSelect:
  case ExpressionKind::RangeSelect:
  case ExpressionKind::MemberAccess:
    out.pushLsp(expr, evalCtx);
    break;
  case ExpressionKind::Concatenation: {
    // Per LRM, operands of a packed concatenation are listed MSB-first;
    // walk in reverse so the LSB operand is appended first and concatLo
    // == 0 is the LSB. Unpacked-array/struct/string concats reuse this
    // path and rely on getSelectableWidth() for per-operand sizing.
    auto const &concat = expr.as<ConcatenationExpression>();
    auto const &operands = concat.operands();
    for (auto it = operands.rbegin(); it != operands.rend(); ++it) {
      buildInto(out, **it, evalCtx);
    }
    break;
  }
  case ExpressionKind::Replication: {
    auto const &rep = expr.as<ReplicationExpression>();
    auto countConst = rep.count().eval(evalCtx);
    if (!countConst.isInteger()) {
      // Non-constant count — treat as opaque.
      out.pushOpaque(expr);
      break;
    }
    auto maybeCount = countConst.integer().as<int64_t>();
    if (!maybeCount) {
      // Count overflowed int64_t — fall back to opaque so the slice
      // list width still matches the expression's selectable width.
      out.pushOpaque(expr);
      break;
    }
    int64_t count = *maybeCount;
    // Zero-count replications are legal SV and produce no bits;
    // negative counts shouldn't reach here post-elaboration but are
    // defended against.
    if (count <= 0) {
      break;
    }
    for (int64_t i = 0; i < count; ++i) {
      buildInto(out, rep.concat(), evalCtx);
    }
    break;
  }
  default:
    out.pushOpaque(expr);
    break;
  }
}

} // namespace

auto BitSliceList::width() const -> uint64_t {
  return slices.empty() ? 0 : slices.back().concatHi;
}

void BitSliceList::pushOpaque(const Expression &expr) {
  auto w = exprWidth(expr);
  if (w == 0) {
    return;
  }
  auto lo = width();
  assert(lo == width());
  BitSlice slice{lo, lo + w, {}};
  slice.sources.emplace_back(BitSliceSource::makeOpaque(expr));
  slices.emplace_back(std::move(slice));
}

void BitSliceList::pushLsp(const Expression &expr, EvalContext &evalCtx) {
  auto w = exprWidth(expr);
  if (w == 0) {
    return;
  }
  // TEMPORARY PLACEHOLDER — Task 2.1 replaces this with a caller-supplied
  // BumpAllocator. Hazards in the current form:
  //   - pathStorage is a function-local static thread_local: it grows
  //     monotonically for the lifetime of the thread and is never freed.
  //   - A BitSlice holding a ValuePath* from this storage must not outlive
  //     the thread that produced it; crossing thread boundaries dangles.
  // Do not call pushLsp from code paths outside the unit tests until
  // Task 2.1 lands.
  static thread_local std::vector<std::unique_ptr<ValuePath>> pathStorage;
  auto owned = std::make_unique<ValuePath>(expr, evalCtx);
  auto lo = width();
  assert(lo == width());
  BitSlice slice{lo, lo + w, {}};
  slice.sources.emplace_back(BitSliceSource::makeLsp(*owned));
  slices.emplace_back(std::move(slice));
  pathStorage.emplace_back(std::move(owned));
}

auto BitSliceList::build(const Expression &expr, EvalContext &evalCtx)
    -> BitSliceList {
  BitSliceList result;
  buildInto(result, expr, evalCtx);
  return result;
}

} // namespace slang::netlist
