#include "netlist/NetlistGraph.hpp"

#include "slang/ast/symbols/ValueSymbol.h"

using namespace slang::netlist;

auto NetlistGraph::lookup(std::string_view name) const -> NetlistNode * {
  auto compare = [&](const std::unique_ptr<NetlistNode> &node) {
    switch (node->kind) {
    case NodeKind::Port:
      return node->as<Port>().symbol.internalSymbol->getHierarchicalPath() ==
             name;
    case NodeKind::Variable:
      return node->as<Variable>().symbol.getHierarchicalPath() == name;
    case NodeKind::State:
      return node->as<State>().symbol.getHierarchicalPath() == name;
    default:
      return false;
    }
  };
  auto it = std::ranges::find_if(*this, compare);
  return it != this->end() ? it->get() : nullptr;
}
