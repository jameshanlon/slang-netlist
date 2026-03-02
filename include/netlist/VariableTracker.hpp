#pragma once

#include "netlist/NetlistNode.hpp"

#include "slang/util/ConcurrentMap.h"
#include "slang/util/IntervalMap.h"

namespace slang::netlist {

/// Track netlist nodes that represent ranges of variables.
///
/// Inserts happen during Phase 1 (sequential). Lookups may happen
/// concurrently during Phase 2 and are lock-free reads into the
/// concurrent map.
struct VariableTracker {
  using VariableMap = IntervalMap<int32_t, NetlistNode *>;

  VariableTracker() : alloc(ba) {}

  /// Insert a new symbol with a node that maps to the specified bounds.
  /// Must only be called during the sequential phase.
  auto insert(ast::Symbol const &symbol, DriverBitRange bounds,
              NetlistNode &node) {
    variables.try_emplace(&symbol, VariableMap());
    variables.visit(&symbol, [&](auto &pair) {
      pair.second.insert(bounds.toPair(), &node, alloc);
    });
  }

  /// Lookup a symbol and return the node for the matching range.
  auto lookup(ast::Symbol const &symbol, DriverBitRange bounds) const
      -> NetlistNode * {
    NetlistNode *result = nullptr;
    variables.cvisit(&symbol, [&](const auto &pair) {
      auto const &map = pair.second;
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
      auto const &map = pair.second;
      for (auto it = map.begin(); it != map.end(); it++) {
        result.push_back(*it);
      }
    });
    return result;
  }

private:
  BumpAllocator ba;
  VariableMap::allocator_type alloc;
  concurrent_map<ast::Symbol const *, VariableMap> variables;
};

} // namespace slang::netlist
