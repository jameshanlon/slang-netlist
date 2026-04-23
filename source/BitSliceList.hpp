#pragma once

#include "BitSlice.hpp"

#include <cstdint>
#include <vector>

namespace slang {
class BumpAllocator;
} // namespace slang

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
  ///
  /// When @p enabled is false, returns a single opaque slice covering the
  /// expression's full width instead of structurally decomposing it. Used
  /// as a kill-switch so callers can honour the `--resolve-assign-bits`
  /// CLI option without duplicating the fallback at each call site.
  ///
  /// The caller owns @p alloc; every `ValuePath` referenced by an LSP
  /// source in the returned list is allocated from it and remains valid
  /// for the allocator's lifetime.
  static auto build(const ast::Expression &expr, ast::EvalContext &evalCtx,
                    BumpAllocator &alloc, bool enabled = true) -> BitSliceList;

  auto size() const -> size_t { return slices.size(); }
  auto width() const -> uint64_t;

  auto operator[](size_t i) const -> const BitSlice & { return slices[i]; }
  auto begin() const { return slices.begin(); }
  auto end() const { return slices.end(); }

  // Helpers used by build() — see BitSliceList.cpp.
  void pushOpaque(const ast::Expression &expr);
  void pushLsp(const ast::Expression &expr, ast::EvalContext &evalCtx,
               BumpAllocator &alloc);
  void pushPaddingSlice(BitSlice slice);

  // Public so callers outside `build()` (e.g. NetlistBuilder's formal-side
  // port slicelist construction) can build lists imperatively via the
  // push* helpers.
  BitSliceList() = default;

private:
  std::vector<BitSlice> slices;
};

/// A zipped segment covering `[concatLo, concatHi)` in a common bit
/// space. `lhsSources` / `rhsSources` hold copies of the sources from
/// whichever slice in each input list contained this segment's low
/// bit. LSP-typed sources retain their original concat-space bounds;
/// `driveLhsLspSegment` / `driveRhsLspSegment` narrow the driven range
/// to the segment at consumption time.
struct Segment {
  uint64_t concatLo;
  uint64_t concatHi;
  SmallVector<BitSliceSource, 2> lhsSources;
  SmallVector<BitSliceSource, 2> rhsSources;

  auto width() const -> uint64_t { return concatHi - concatLo; }
};

/// Align two slicelists of equal total width onto a common cut-point
/// grid. Each resulting segment carries sources from whichever slice
/// in each side covers its low bit. Precondition:
/// `lhs.width() == rhs.width()`.
auto alignSegments(const BitSliceList &lhs, const BitSliceList &rhs)
    -> std::vector<Segment>;

} // namespace slang::netlist
