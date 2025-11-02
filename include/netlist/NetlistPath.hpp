#pragma once

#include "netlist/NetlistBuilder.hpp"

#include <algorithm>
#include <vector>

namespace slang::netlist {

/// A class representing a path traversing nodes in the netlist.
class NetlistPath {
public:
  using NodeListType = std::vector<NetlistNode const *>;
  using iterator = typename NodeListType::iterator;
  using const_iterator = typename NodeListType::const_iterator;

  NetlistPath() = default;

  NetlistPath(NodeListType nodes) : nodes(std::move(nodes)) {};

  auto begin() const -> const_iterator { return nodes.begin(); }
  auto end() const -> const_iterator { return nodes.end(); }
  auto begin() -> iterator { return nodes.begin(); }
  auto end() -> iterator { return nodes.end(); }

  auto operator[](size_t index) const -> NetlistNode const * {
    return nodes[index];
  }

  void add(NetlistNode &node) { nodes.push_back(&node); }

  void add(NetlistNode *node) { nodes.push_back(node); }

  void reverse() { std::ranges::reverse(nodes); }

  auto size() const -> size_t { return nodes.size(); }

  auto empty() const -> bool { return nodes.empty(); }
  void clear() { nodes.clear(); }

  auto front() const -> NetlistNode const * { return nodes.front(); }

  auto back() const -> NetlistNode const * { return nodes.back(); }

private:
  NodeListType nodes;
};

} // namespace slang::netlist
