#include "BitSliceList.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/ValuePath.h"
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
  // Everything is opaque for now; subsequent tasks teach build() about
  // each structural expression kind.
  switch (expr.kind) {
  case ExpressionKind::NamedValue:
  case ExpressionKind::HierarchicalValue:
  case ExpressionKind::ElementSelect:
  case ExpressionKind::RangeSelect:
  case ExpressionKind::MemberAccess:
    result.pushLsp(expr, evalCtx);
    break;
  default:
    result.pushOpaque(expr);
    break;
  }
  return result;
}

} // namespace slang::netlist
