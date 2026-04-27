#pragma once

#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace slang::netlist {

/// Side table mapping a ValueSymbol (typically the internal symbol of
/// a formal port) to the sorted set of bit offsets at which external
/// concats have introduced cut points. Cuts are bit offsets within
/// the symbol's selectable range; the trivial 0 / width endpoints are
/// not stored.
///
/// Populated during Phase 1 (sequential, in port-connection
/// processing) and read by `BitSliceList::pushLsp` during Phase 2 DFA
/// (which is read-only). No locking is required given that ordering.
class CutRegistry {
public:
  /// Union @p cuts into the existing cut set for @p symbol. Endpoints
  /// (0 and the symbol's selectable width) are dropped.
  void addCuts(ast::ValueSymbol const &symbol, std::vector<uint64_t> cuts) {
    if (cuts.empty()) {
      return;
    }
    auto width = symbol.getType().getSelectableWidth();
    auto &set = entries[&symbol];
    for (auto cut : cuts) {
      if (cut > 0 && cut < width) {
        set.push_back(cut);
      }
    }
    std::sort(set.begin(), set.end());
    set.erase(std::unique(set.begin(), set.end()), set.end());
  }

  /// Return the sorted cut offsets for @p symbol, or nullptr when no
  /// cuts are registered.
  auto cutsFor(ast::ValueSymbol const &symbol) const
      -> std::vector<uint64_t> const * {
    auto it = entries.find(&symbol);
    return it == entries.end() ? nullptr : &it->second;
  }

private:
  std::unordered_map<ast::ValueSymbol const *, std::vector<uint64_t>> entries;
};

} // namespace slang::netlist
