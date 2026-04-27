#pragma once

#include "netlist/DriverBitRange.hpp"
#include "netlist/NetlistGraph.hpp"

#include "slang/text/FormatBuffer.h"

namespace slang::netlist {

/// A utility class for rendering a netlist graph in DOT format.
struct NetlistDot {

  static auto render(NetlistGraph const &netlist, FormatBuffer &buffer) {
    buffer.append("digraph {\n");
    buffer.append("  node [shape=record];\n");
    for (auto &node : netlist) {
      switch (node->kind) {
      case NodeKind::Port: {
        auto &portNode = node->as<Port>();
        buffer.format("  N{} [label=\"{} port {}\"]\n", node->ID,
                      toString(portNode.direction), portNode.name);
        break;
      }
      case NodeKind::Variable: {
        auto &varNode = node->as<Variable>();
        buffer.format("  N{} [label=\"Variable {}\"]\n", node->ID,
                      varNode.name);
        break;
      }
      case NodeKind::Assignment: {
        buffer.format("  N{} [label=\"Assignment\"]\n", node->ID);
        break;
      }
      case NodeKind::Case: {
        buffer.format("  N{} [label=\"Case\"]\n", node->ID);
        break;
      }
      case NodeKind::Conditional: {
        buffer.format("  N{} [label=\"Conditional\"]\n", node->ID);
        break;
      }
      case NodeKind::Merge: {
        buffer.format("  N{} [label=\"Merge\"]\n", node->ID);
        break;
      }
      case NodeKind::State: {
        auto &state = node->as<State>();
        buffer.format("  N{} [label=\"{} {}\"]\n", node->ID, state.name,
                      toString(state.bounds));
        break;
      }
      case NodeKind::Constant: {
        auto &constNode = node->as<Constant>();
        buffer.format("  N{} [label=\"Const {}\"]\n", node->ID,
                      constNode.value.toString());
        break;
      }
      default:
        SLANG_UNREACHABLE;
      }
    }
    for (auto const &node : netlist) {
      for (auto const &edge : node->getOutEdges()) {
        if (edge->disabled) {
          continue;
        }
        if (!edge->symbol.empty()) {
          buffer.format("  N{} -> N{} [label=\"{}{}\"]\n", node->ID,
                        edge->getTargetNode().ID, edge->symbol.name,
                        toString(edge->bounds));
        } else {
          buffer.format("  N{} -> N{}\n", node->ID, edge->getTargetNode().ID);
        }
      }
    }
    buffer.append("}\n");
  }
};

} // namespace slang::netlist
