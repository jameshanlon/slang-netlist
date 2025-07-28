#pragma once

#include "netlist/NetlistGraph.hpp"
#include "slang/text/FormatBuffer.h"

namespace slang::netlist {

struct NetlistDot {

  static auto render(const NetlistGraph &netlist, FormatBuffer &buffer) {
    buffer.append("digraph {\n");
    buffer.append("  node [shape=record];\n");
    for (auto &node : netlist) {
      switch (node->kind) {
      case NodeKind::Port: {
        auto &portNode = node->as<Port>();
        auto name = portNode.internalSymbol == nullptr
                        ? ""
                        : portNode.internalSymbol->name;
        buffer.format("  N{} [label=\"{} port {}\"]\n", node->ID,
                      toString(portNode.direction), name);
        break;
      }
      case NodeKind::Assignment: {
        auto &assignment = node->as<Assignment>();
        buffer.format("  N{} [label=\"Assignment\"]\n", node->ID);
        break;
      }
      case NodeKind::Case: {
        auto &caseNode = node->as<Case>();
        buffer.format("  N{} [label=\"Case\"]\n", node->ID);
        break;
      }
      case NodeKind::Conditional: {
        auto &conditional = node->as<Conditional>();
        buffer.format("  N{} [label=\"Conditional\"]\n", node->ID);
        break;
      }
      case NodeKind::Merge: {
        auto &merge = node->as<Merge>();
        buffer.format("  N{} [label=\"Merge\"]\n", node->ID);
        break;
      }
      default:
        SLANG_UNREACHABLE;
      }
    }
    for (auto &node : netlist) {
      for (auto &edge : node->getOutEdges()) {
        if (edge->disabled) {
          continue;
        }
        if (edge->symbol) {
          buffer.format("  N{} -> N{} [label=\"{}[{}:{}]\"]\n", node->ID,
                        edge->getTargetNode().ID, edge->symbol->name,
                        edge->bounds.second, edge->bounds.first);
        } else {
          buffer.format("  N{} -> N{}\n", node->ID, edge->getTargetNode().ID);
        }
      }
    }
    buffer.append("}\n");
  }
};

} // namespace slang::netlist
