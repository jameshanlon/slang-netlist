#pragma once

#include "netlist/NetlistNode.hpp"

#include <algorithm>
#include <vector>

namespace slang::netlist {

/// A class representing a path traversing nodes in a netlist graph.
///
/// Templated on NodeType so the same implementation works for both
/// NetlistNode (live graph) and FlatNetlistNode (deserialised graph).
template <class NodeType> class BasicPath {
public:
  using NodeListType = std::vector<NodeType const *>;
  using iterator = typename NodeListType::iterator;
  using const_iterator = typename NodeListType::const_iterator;

  BasicPath() = default;

  BasicPath(NodeListType nodes) : nodes(std::move(nodes)) {}

  auto begin() const -> const_iterator { return nodes.begin(); }
  auto end() const -> const_iterator { return nodes.end(); }
  auto begin() -> iterator { return nodes.begin(); }
  auto end() -> iterator { return nodes.end(); }

  auto operator[](size_t index) const -> NodeType const * {
    return nodes[index];
  }

  void add(NodeType &node) { nodes.push_back(&node); }

  void add(NodeType *node) { nodes.push_back(node); }

  void reverse() { std::ranges::reverse(nodes); }

  auto size() const -> size_t { return nodes.size(); }

  auto empty() const -> bool { return nodes.empty(); }

  void clear() { nodes.clear(); }

  auto front() const -> NodeType const * { return nodes.front(); }

  auto back() const -> NodeType const * { return nodes.back(); }

private:
  NodeListType nodes;
};

/// Path over a live NetlistGraph.
using NetlistPath = BasicPath<NetlistNode>;

} // namespace slang::netlist
