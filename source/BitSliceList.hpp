#pragma once

#include "BitSlice.hpp"

#include <cstdint>
#include <vector>

namespace slang::ast {
class EvalContext;
class Expression;
} // namespace slang::ast

namespace slang::netlist {

/// A flat list of BitSlices covering a contiguous bit range
/// `[0, width())` of some decomposed expression.
class BitSliceList {
public:
  /// Decomposes @p expr into a list of bit slices covering its full width.
  ///
  /// Recognised structural kinds (Concatenation, Replication, Conversion,
  /// equal-width ConditionalOp) are walked and their operands inlined.
  /// Everything else produces a single Opaque slice covering the
  /// expression's full `getSelectableWidth()`.
  static auto build(const ast::Expression &expr, ast::EvalContext &evalCtx)
      -> BitSliceList;

  auto size() const -> size_t { return slices.size(); }
  auto width() const -> uint64_t;

  auto operator[](size_t i) const -> const BitSlice & { return slices[i]; }
  auto begin() const { return slices.begin(); }
  auto end() const { return slices.end(); }

  // Helpers used by build() — see BitSliceList.cpp.
  void pushOpaque(const ast::Expression &expr);
  void pushLsp(const ast::Expression &expr, ast::EvalContext &evalCtx);

private:
  std::vector<BitSlice> slices;

  // Only build() constructs BitSliceLists.
  BitSliceList() = default;
};

} // namespace slang::netlist
