#pragma once

#include "netlist/NetlistGraph.hpp"

#include "slang/util/IntervalMap.h"

namespace slang::netlist {

/// Track netlist nodes that represent ranges of variables.
struct VariableTracker {
  using VariableMap = IntervalMap<uint32_t, NetlistNode *>;

  VariableTracker() : alloc(ba) {}

  /// Insert a new symbol with a node that maps to the specified bounds.
  auto insert(ast::Symbol const &symbol, DriverBitRange bounds,
              NetlistNode &node) {
    if (!variables.contains(&symbol)) {
      variables.emplace(&symbol, VariableMap());
    }
    variables[&symbol].insert(bounds, &node, alloc);
  }

  /// Lookup a symbol and return the node for the matching range.
  auto lookup(ast::Symbol const &symbol, DriverBitRange bounds) const
      -> NetlistNode * {
    if (variables.contains(&symbol)) {
      auto &map = variables.find(&symbol)->second;
      for (auto it = map.find(bounds); it != map.end(); it++) {
        if (it.bounds() == bounds) {
          return *it;
        }
      }
    }
    return nullptr;
  }

  /// Lookup a symbol and return the nodes for all mapped ranges.
  auto lookup(ast::Symbol const &symbol) const -> std::vector<NetlistNode *> {
    std::vector<NetlistNode *> result;
    if (variables.contains(&symbol)) {
      auto &map = variables.find(&symbol)->second;
      for (auto it = map.begin(); it != map.end(); it++) {
        result.push_back(*it);
      }
    }
    return result;
  }

private:
  BumpAllocator ba;
  VariableMap::allocator_type alloc;
  std::map<ast::Symbol const *, VariableMap> variables;
};

} // namespace slang::netlist
