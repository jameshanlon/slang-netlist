#pragma once

#include "netlist/NetlistGraph.hpp"
#include <algorithm>
#include <vector>

namespace slang::netlist {

/// A class represening a path traversing nodes in the netlist.
class NetlistPath {
public:
  using NodeListType = std::vector<NetlistNode const *>;
  using iterator = typename NodeListType::iterator;
  using const_iterator = typename NodeListType::const_iterator;

  NetlistPath() = default;

  NetlistPath(NodeListType nodes) : nodes(std::move(nodes)) {};

  const_iterator begin() const { return nodes.begin(); }
  const_iterator end() const { return nodes.end(); }
  iterator begin() { return nodes.begin(); }
  iterator end() { return nodes.end(); }

  auto operator[](size_t index) const -> NetlistNode const * {
    return nodes[index];
  }

  void add(NetlistNode &node) { nodes.push_back(&node); }

  void add(NetlistNode *node) { nodes.push_back(node); }

  void reverse() { std::ranges::reverse(nodes); }

  size_t size() const { return nodes.size(); }

  bool empty() const { return nodes.empty(); }
  void clear() { nodes.clear(); }

  auto front() const -> NetlistNode const * { return nodes.front(); }

  auto back() const -> NetlistNode const * { return nodes.back(); }

private:
  NodeListType nodes;
};

} // namespace slang::netlist
