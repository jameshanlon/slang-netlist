#pragma once

#include <algorithm>
#include <vector>

#include "netlist/NetlistGraph.hpp"

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

  void add(NetlistNode &node) { nodes.push_back(&node); }

  void add(NetlistNode *node) { nodes.push_back(node); }

  void reverse() { std::ranges::reverse(nodes); }

  size_t size() const { return nodes.size(); }

  bool empty() const { return nodes.empty(); }
  void clear() { nodes.clear(); }

  /// Return index within the path if a variable reference matches the
  /// specified syntax (ie including the hierarchical reference to the
  /// variable name and selectors) and appears on the left-hand side of an
  /// assignment (ie a target).
  std::optional<size_t> findVariable(std::string syntax) {
    return std::nullopt;
  }

private:
  NodeListType nodes;
};

} // namespace slang::netlist
