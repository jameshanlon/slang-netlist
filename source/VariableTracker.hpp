#pragma once

#include "netlist/NetlistNode.hpp"

#include "slang/util/ConcurrentMap.h"
#include "slang/util/IntervalMap.h"

#include <memory>

namespace slang::netlist {

/// Track netlist nodes that represent ranges of variables.
///
/// Inserts happen during Phase 1 (sequential) and Phase 2 (from
/// mergeDrivers for sequential edges). Lookups may happen concurrently
/// during Phase 2 and are lock-free reads into the concurrent map.
struct VariableTracker {
  using VariableMap = IntervalMap<int32_t, NetlistNode *>;

  /// Per-entry storage bundling a VariableMap with its own allocator.
  /// The BumpAllocator is heap-allocated so the PoolAllocator reference
  /// remains stable across concurrent_flat_map rehashes.
  struct VariableEntry {
    std::unique_ptr<BumpAllocator> ba;
    VariableMap::allocator_type alloc;
    VariableMap map;

    VariableEntry()
        : ba(std::make_unique<BumpAllocator>()), alloc(*ba) {}

    // Move constructor for concurrent_flat_map rehash. unique_ptr
    // transfer keeps the BumpAllocator at the same address; a fresh
    // PoolAllocator is built from it (old free list is lost but
    // backing memory stays valid).
    VariableEntry(VariableEntry &&other) noexcept
        : ba(std::move(other.ba)),
          alloc(*ba),
          map(std::move(other.map)) {}

    VariableEntry(VariableEntry const &) = delete;
    VariableEntry &operator=(VariableEntry const &) = delete;
    VariableEntry &operator=(VariableEntry &&) = delete;
  };

  /// Insert a new symbol with a node that maps to the specified bounds.
  /// Thread safety: safe to call concurrently. The concurrent_map
  /// serializes per-key access; each entry owns its own allocator.
  auto insert(ast::Symbol const &symbol, DriverBitRange bounds,
              NetlistNode &node) {
    variables.try_emplace(&symbol);
    variables.visit(&symbol, [&](auto &pair) {
      pair.second.map.insert(bounds.toPair(), &node, pair.second.alloc);
    });
  }

  /// Lookup a symbol and return the node for the matching range.
  auto lookup(ast::Symbol const &symbol, DriverBitRange bounds) const
      -> NetlistNode * {
    NetlistNode *result = nullptr;
    variables.cvisit(&symbol, [&](const auto &pair) {
      auto const &map = pair.second.map;
      for (auto it = map.find(bounds.toPair()); it != map.end(); it++) {
        if (it.bounds() == bounds) {
          result = *it;
          return;
        }
      }
    });
    return result;
  }

  /// Lookup a symbol and return the nodes for all mapped ranges.
  auto lookup(ast::Symbol const &symbol) const -> std::vector<NetlistNode *> {
    std::vector<NetlistNode *> result;
    variables.cvisit(&symbol, [&](const auto &pair) {
      auto const &map = pair.second.map;
      for (auto it = map.begin(); it != map.end(); it++) {
        result.push_back(*it);
      }
    });
    return result;
  }

private:
  concurrent_map<ast::Symbol const *, VariableEntry> variables;
};

} // namespace slang::netlist
