#include "netlist/NetlistGraph.hpp"

#include <algorithm>
#include <memory>
#include <string_view>

using namespace slang::netlist;

auto NetlistGraph::lookup(std::string_view name) const -> NetlistNode * {
  auto compare = [&](const std::unique_ptr<NetlistNode> &node) -> bool {
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
  auto it = std::ranges::find_if(*this, compare);
  return it != this->end() ? it->get() : nullptr;
}
