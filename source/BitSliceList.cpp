#include "BitSliceList.hpp"

#include "CutRegistry.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
#include "slang/numeric/ConstantValue.h"
#include "slang/numeric/SVInt.h"
#include "slang/util/BumpAllocator.h"

#include <algorithm>
#include <cassert>
#include <vector>

using namespace slang;
using namespace slang::ast;

namespace slang::netlist {

namespace {

/// Return the selectable bit width of @p expr's type.
auto exprWidth(const Expression &expr) -> uint64_t {
  return expr.type->getSelectableWidth();
}

auto collectCuts(const BitSliceList &list) -> std::vector<uint64_t> {
  std::vector<uint64_t> cuts;
  cuts.reserve(list.size() + 1);
  cuts.push_back(0);
  for (auto const &s : list) {
    cuts.push_back(s.concatHi);
  }
  return cuts;
}

auto mergeCuts(std::vector<uint64_t> a, std::vector<uint64_t> const &b)
    -> std::vector<uint64_t> {
  a.insert(a.end(), b.begin(), b.end());
  std::sort(a.begin(), a.end());
  a.erase(std::unique(a.begin(), a.end()), a.end());
  return a;
}

/// Return the slice in @p list whose range contains bit @p bit, or nullptr.
auto findSliceContaining(const BitSliceList &list, uint64_t bit)
    -> const BitSlice * {
  for (auto const &s : list) {
    if (bit >= s.concatLo && bit < s.concatHi) {
      return &s;
    }
  }
  return nullptr;
}

void buildInto(BitSliceList &out, const Expression &expr, EvalContext &evalCtx,
               BumpAllocator &alloc, CutRegistry const *cuts) {
  switch (expr.kind) {
  case ExpressionKind::NamedValue:
  case ExpressionKind::HierarchicalValue:
  case ExpressionKind::ElementSelect:
  case ExpressionKind::RangeSelect:
  case ExpressionKind::MemberAccess:
    out.pushLsp(expr, evalCtx, alloc, cuts);
    break;
  case ExpressionKind::Concatenation: {
    // Per LRM, operands of a packed concatenation are listed MSB-first;
    // walk in reverse so the LSB operand is appended first and concatLo
    // == 0 is the LSB. Unpacked-array/struct/string concats reuse this
    // path and rely on getSelectableWidth() for per-operand sizing.
    auto const &concat = expr.as<ConcatenationExpression>();
    auto const &operands = concat.operands();
    for (auto it = operands.rbegin(); it != operands.rend(); ++it) {
      buildInto(out, **it, evalCtx, alloc, cuts);
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
      buildInto(out, rep.concat(), evalCtx, alloc, cuts);
    }
    break;
  }
  case ExpressionKind::Conversion: {
    auto const &conv = expr.as<ConversionExpression>();
    auto const &inner = conv.operand();
    auto outerWidth = exprWidth(expr);
    auto innerWidth = exprWidth(inner);
    // Fold a constant operand straight to a Constant slice. Catches
    // narrowing conversions of literals (e.g. `b = 1` where b is 1
    // bit; the parser inserts a 32-bit int -> 1-bit logic narrowing
    // conversion) which would otherwise fall through to pushOpaque.
    if (expr.type != nullptr && expr.type->isIntegral()) {
      ConstantValue cv = expr.eval(evalCtx);
      if (cv && cv.isInteger() && cv.integer().getBitWidth() == outerWidth) {
        out.pushConstant(std::move(cv), expr);
        break;
      }
    }
    if (outerWidth == innerWidth) {
      buildInto(out, inner, evalCtx, alloc, cuts);
      break;
    }
    if (outerWidth > innerWidth) {
      // Emit operand's slices first (LSB), then padding (MSB).
      buildInto(out, inner, evalCtx, alloc, cuts);
      uint64_t lo = out.width();
      uint64_t padWidth = outerWidth - innerWidth;
      BitSlice pad{lo, lo + padWidth, {}};
      // Stricter than the propagated-type rule in LRM 11.8.2, which
      // only requires the outer type to be signed; keep the conjunctive
      // form until a consumer reads padIsSignExtension.
      bool isSignExt = inner.type->isSigned() && conv.type->isSigned();
      if (isSignExt) {
        pad.sources.emplace_back(BitSliceSource::makePadding(isSignExt));
      } else {
        // Zero-extension drives the padding bits with a constant zero.
        ConstantValue zero =
            SVInt(static_cast<bitwidth_t>(padWidth), 0u, /*isSigned=*/false);
        pad.sources.emplace_back(
            BitSliceSource::makeConstant(std::move(zero), lo, lo + padWidth));
      }
      out.pushPaddingSlice(std::move(pad));
      break;
    }
    // Narrowing conversion — the upper bits are dropped, which we can't
    // easily represent in a pass-through slicelist. Fall back to opaque
    // for correctness.
    out.pushOpaque(expr);
    break;
  }
  case ExpressionKind::ConditionalOp: {
    auto const &cond = expr.as<ConditionalExpression>();
    auto const &left = cond.left();
    auto const &right = cond.right();
    if (exprWidth(left) != exprWidth(right) ||
        exprWidth(left) != exprWidth(expr)) {
      out.pushOpaque(expr);
      break;
    }
    // Pattern-bearing conditions (`x matches P ? a : b`) would drop
    // dependency edges inside the pattern if we only attributed the
    // predicate expression, so fall back to opaque when any condition
    // has a pattern. The SV grammar does not allow multi-condition `?:`
    // but we defend against it symmetrically.
    if (cond.conditions.size() != 1 || cond.conditions[0].pattern != nullptr) {
      out.pushOpaque(expr);
      break;
    }
    BitSliceList l =
        BitSliceList::build(left, evalCtx, alloc, /*enabled=*/true, cuts);
    BitSliceList r =
        BitSliceList::build(right, evalCtx, alloc, /*enabled=*/true, cuts);
    if (l.width() != r.width()) {
      out.pushOpaque(expr);
      break;
    }
    auto unifiedCuts = mergeCuts(collectCuts(l), collectCuts(r));
    // Condition-opaque source, shared by every unified slice. For a
    // plain `?:` the `conditions` span has exactly one entry whose
    // `expr` is the predicate.
    auto condSource = BitSliceSource::makeOpaque(*cond.conditions[0].expr);
    uint64_t baseOffset = out.width();
    for (size_t i = 0; i + 1 < unifiedCuts.size(); ++i) {
      BitSlice unified{
          baseOffset + unifiedCuts[i], baseOffset + unifiedCuts[i + 1], {}};
      uint64_t mid = unifiedCuts[i];
      if (auto const *ls = findSliceContaining(l, mid)) {
        for (auto const &src : ls->sources) {
          unified.sources.emplace_back(src);
        }
      }
      if (auto const *rs = findSliceContaining(r, mid)) {
        for (auto const &src : rs->sources) {
          unified.sources.emplace_back(src);
        }
      }
      unified.sources.emplace_back(condSource);
      out.pushPaddingSlice(std::move(unified));
    }
    break;
  }
  default:
    // Try constant-folding the expression. A successful fold produces a
    // Constant slice; the consumer materializes a Constant netlist node
    // and an edge into the consuming sink. Restricted to integral types
    // so the slice width matches the expression's selectable width.
    if (expr.type != nullptr && expr.type->isIntegral()) {
      ConstantValue cv = expr.eval(evalCtx);
      if (cv && cv.isInteger() &&
          cv.integer().getBitWidth() == exprWidth(expr)) {
        out.pushConstant(std::move(cv), expr);
        break;
      }
    }
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
  BitSlice slice{lo, lo + w, {}};
  slice.sources.emplace_back(BitSliceSource::makeOpaque(expr));
  slices.emplace_back(std::move(slice));
}

void BitSliceList::pushLsp(const Expression &expr, EvalContext &evalCtx,
                           BumpAllocator &alloc, CutRegistry const *cuts) {
  auto w = exprWidth(expr);
  if (w == 0) {
    return;
  }
  auto *path = alloc.emplace<ValuePath>(expr, evalCtx);
  auto lo = width();

  // Split the LSP at any cut hints that fall inside its bounds. Each
  // sub-slice keeps the full-LSP `srcLo`/`srcHi` so consumers can
  // still recover the LSP-internal bit via `seg.concatLo - src.srcLo`.
  auto const *rootSymbol = cuts ? path->rootSymbol() : nullptr;
  auto const *hints = rootSymbol ? cuts->cutsFor(*rootSymbol) : nullptr;
  if (hints != nullptr) {
    uint64_t lspLo = static_cast<uint64_t>(path->lspBounds.first);
    uint64_t lspHi = static_cast<uint64_t>(path->lspBounds.second) + 1;
    auto first = std::upper_bound(hints->begin(), hints->end(), lspLo);
    auto last = std::lower_bound(hints->begin(), hints->end(), lspHi);
    if (first != last) {
      uint64_t srcLo = lo;
      uint64_t srcHi = lo + (lspHi - lspLo);
      uint64_t prev = lspLo;
      auto emit = [&](uint64_t segLo, uint64_t segHi) {
        uint64_t outLo = lo + (segLo - lspLo);
        uint64_t outHi = lo + (segHi - lspLo);
        BitSlice slice{outLo, outHi, {}};
        slice.sources.emplace_back(
            BitSliceSource::makeLsp(*path, srcLo, srcHi));
        slices.emplace_back(std::move(slice));
      };
      for (auto it = first; it != last; ++it) {
        emit(prev, *it);
        prev = *it;
      }
      emit(prev, lspHi);
      return;
    }
  }

  BitSlice slice{lo, lo + w, {}};
  slice.sources.emplace_back(BitSliceSource::makeLsp(*path, lo, lo + w));
  slices.emplace_back(std::move(slice));
}

void BitSliceList::pushPaddingSlice(BitSlice slice) {
  slices.emplace_back(std::move(slice));
}

void BitSliceList::pushConstant(ConstantValue value, const Expression &expr) {
  auto w = exprWidth(expr);
  if (w == 0) {
    return;
  }
  auto lo = width();
  BitSlice slice{lo, lo + w, {}};
  slice.sources.emplace_back(
      BitSliceSource::makeConstant(std::move(value), lo, lo + w, &expr));
  slices.emplace_back(std::move(slice));
}

auto BitSliceList::build(const Expression &expr, EvalContext &evalCtx,
                         BumpAllocator &alloc, bool enabled,
                         CutRegistry const *cuts) -> BitSliceList {
  BitSliceList result;
  if (!enabled) {
    result.pushOpaque(expr);
    return result;
  }
  buildInto(result, expr, evalCtx, alloc, cuts);
  return result;
}

auto alignSegments(const BitSliceList &lhs, const BitSliceList &rhs)
    -> std::vector<Segment> {
  assert(lhs.width() == rhs.width());
  std::vector<Segment> result;
  auto cuts = mergeCuts(collectCuts(lhs), collectCuts(rhs));
  // collectCuts always seeds with 0, so cuts has >= 1 element and
  // cuts.size() - 1 is safe.
  result.reserve(cuts.size() - 1);
  for (size_t i = 0; i + 1 < cuts.size(); ++i) {
    Segment seg{cuts[i], cuts[i + 1], {}, {}};
    uint64_t mid = cuts[i];
    if (auto const *ls = findSliceContaining(lhs, mid)) {
      for (auto const &src : ls->sources) {
        seg.lhsSources.emplace_back(src);
      }
    }
    if (auto const *rs = findSliceContaining(rhs, mid)) {
      for (auto const &src : rs->sources) {
        seg.rhsSources.emplace_back(src);
      }
    }
    result.emplace_back(std::move(seg));
  }
  return result;
}

} // namespace slang::netlist
