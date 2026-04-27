#pragma once

#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace slang::netlist {

/// Side table that maps a ValueSymbol (typically the internal symbol of
/// a formal port) to the set of bit offsets at which external concats
/// have introduced cut points. Cuts are bit offsets within the symbol's
/// selectable range; the trivial 0 / width endpoints are not stored.
///
/// Populated during Phase 1 (port-connection processing); read by
/// `BitSliceList::pushLsp` during Phase 2 DFA. After Phase 1 ends,
/// `freeze()` is called and the registry becomes read-only.
class CutRegistry {
public:
  /// Union @p cuts into the existing cut set for @p symbol. Endpoints
  /// (0 and the symbol's selectable width) are dropped. Safe to call
  /// from any thread before `freeze()`.
  void addCuts(ast::ValueSymbol const &symbol, std::vector<uint64_t> cuts) {
    if (cuts.empty()) {
      return;
    }
    auto width = symbol.getType().getSelectableWidth();
    std::lock_guard lock(mutex);
    auto &set = entries[&symbol];
    for (auto cut : cuts) {
      if (cut == 0 || cut >= width) {
        continue;
      }
      set.push_back(cut);
    }
    std::sort(set.begin(), set.end());
    set.erase(std::unique(set.begin(), set.end()), set.end());
  }

  /// Mark the registry as immutable. After this point, no further
  /// `addCuts` calls are permitted; readers (`cutsFor`) can run
  /// concurrently without locking.
  void freeze() { frozen = true; }

  /// Return the sorted cut offsets for @p symbol, or nullptr when no
  /// cuts are registered. Cuts do not include the trivial 0 / width
  /// endpoints. Safe during Phase 1 (callers serialize their own
  /// writers vs. this read) and after `freeze()`.
  auto cutsFor(ast::ValueSymbol const &symbol) const
      -> std::vector<uint64_t> const * {
    auto it = entries.find(&symbol);
    if (it == entries.end()) {
      return nullptr;
    }
    return &it->second;
  }

  auto empty() const -> bool { return entries.empty(); }

private:
  std::unordered_map<ast::ValueSymbol const *, std::vector<uint64_t>> entries;
  std::mutex mutex;
  bool frozen = false;
};

} // namespace slang::netlist
