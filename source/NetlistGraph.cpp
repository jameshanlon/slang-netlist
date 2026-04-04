#include "netlist/NetlistGraph.hpp"

#include <algorithm>
#include <memory>
#include <string_view>

using namespace slang::netlist;

auto NetlistGraph::lookup(std::string_view name) const -> NetlistNode * {
  NetlistNode *bestMatch = nullptr;

  auto matches = [&](const std::unique_ptr<NetlistNode> &node) -> bool {
    switch (node->kind) {
    case NodeKind::Port:
      return node->as<Port>().hierarchicalPath == name;
    case NodeKind::Variable:
      return node->as<Variable>().hierarchicalPath == name;
    case NodeKind::State:
      return node->as<State>().hierarchicalPath == name;
    default:
      return false;
    }
  };

  auto rank = [](NodeKind kind) -> int {
    switch (kind) {
    case NodeKind::Port:
      return 0;
    case NodeKind::State:
      return 1;
    case NodeKind::Variable:
      return 2;
    default:
      return 3;
    }
  };

  for (auto const &node : *this) {
    if (!matches(node)) {
      continue;
    }
    if (bestMatch == nullptr || rank(node->kind) < rank(bestMatch->kind)) {
      bestMatch = node.get();
    }
  }

  return bestMatch;
}
