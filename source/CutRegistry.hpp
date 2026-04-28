#pragma once

#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace slang::netlist {

/// Maps a ValueSymbol to the sorted set of bit offsets at which
/// external concats have introduced cut points. Endpoints (0, width)
/// are not stored. Populated during Phase 1 and read during Phase 2.
class CutRegistry {
public:
  /// Union @p cuts into the existing set for @p symbol. Endpoints are
  /// dropped.
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

  /// Return the sorted cuts for @p symbol, or nullptr if none.
  auto cutsFor(ast::ValueSymbol const &symbol) const
      -> std::vector<uint64_t> const * {
    auto it = entries.find(&symbol);
    return it == entries.end() ? nullptr : &it->second;
  }

private:
  std::unordered_map<ast::ValueSymbol const *, std::vector<uint64_t>> entries;
};

} // namespace slang::netlist
