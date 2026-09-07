#pragma once

#include "netlist/DriverBitRange.hpp"
#include "netlist/NetlistGraph.hpp"

#include "common/FormatBuffer.hpp"

#include <string>
#include <unordered_set>

namespace slang::netlist {

/// A utility class for rendering a netlist graph in DOT format.
struct NetlistDot {

  /// Render the whole netlist graph.
  static auto render(NetlistGraph const &netlist, FormatBuffer &buffer) {
    renderImpl(netlist, buffer, nullptr);
  }

  /// Render only the induced subgraph over @p nodes: nodes not in the set are
  /// omitted, and an edge is kept only when both of its endpoints are in the
  /// set. Useful for rendering a fan-in/fan-out cone or a path in isolation.
  static auto render(NetlistGraph const &netlist, FormatBuffer &buffer,
                     std::unordered_set<NetlistNode const *> const &nodes) {
    renderImpl(netlist, buffer, &nodes);
  }

private:
  /// Escape the characters that are structural inside a record-shaped
  /// DOT label.
  static auto escapeLabel(std::string_view text) -> std::string {
    constexpr std::string_view metacharacters = "|<>{}\"\\";
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
      if (metacharacters.find(c) != std::string_view::npos) {
        result.push_back('\\');
      }
      result.push_back(c);
    }
    return result;
  }

  static void writeNode(FormatBuffer &buffer, NetlistNode const &node) {
    switch (node.kind) {
    case NodeKind::Port: {
      auto const &portNode = node.as<Port>();
      buffer.format("  N{} [label=\"{} port {}\"]\n", node.ID,
                    toString(portNode.direction), portNode.name);
      break;
    }
    case NodeKind::Variable: {
      auto const &varNode = node.as<Variable>();
      buffer.format("  N{} [label=\"Variable {}\"]\n", node.ID, varNode.name);
      break;
    }
    case NodeKind::Assignment: {
      buffer.format("  N{} [label=\"Assignment\"]\n", node.ID);
      break;
    }
    case NodeKind::Case: {
      buffer.format("  N{} [label=\"Case\"]\n", node.ID);
      break;
    }
    case NodeKind::Conditional: {
      buffer.format("  N{} [label=\"Conditional\"]\n", node.ID);
      break;
    }
    case NodeKind::Merge: {
      buffer.format("  N{} [label=\"Merge\"]\n", node.ID);
      break;
    }
    case NodeKind::State: {
      auto const &state = node.as<State>();
      buffer.format("  N{} [label=\"{} {}\"]\n", node.ID, state.name,
                    toString(state.bounds));
      break;
    }
    case NodeKind::Constant: {
      auto const &constNode = node.as<Constant>();
      buffer.format("  N{} [label=\"Const {}\"]\n", node.ID,
                    constNode.value.toString());
      break;
    }
    case NodeKind::Operation: {
      auto const &opNode = node.as<Operation>();
      buffer.format("  N{} [label=\"Op {} [{}]\"]\n", node.ID,
                    escapeLabel(toSymbol(opNode.op)), opNode.width);
      break;
    }
    default:
      SLANG_UNREACHABLE;
    }
  }

  static void
  renderImpl(NetlistGraph const &netlist, FormatBuffer &buffer,
             std::unordered_set<NetlistNode const *> const *filter) {
    auto included = [&](NetlistNode const &node) {
      return filter == nullptr || filter->count(&node) != 0;
    };

    buffer.append("digraph {\n");
    buffer.append("  node [shape=record];\n");
    for (auto const &node : netlist) {
      if (!included(*node)) {
        continue;
      }
      writeNode(buffer, *node);
    }
    for (auto const &node : netlist) {
      if (!included(*node)) {
        continue;
      }
      for (auto const &edge : node->getOutEdges()) {
        if (edge->disabled) {
          continue;
        }
        if (!included(edge->getTargetNode())) {
          continue;
        }
        if (edge->symbol != nullptr && !edge->symbol->empty()) {
          buffer.format("  N{} -> N{} [label=\"{}{}\"]\n", node->ID,
                        edge->getTargetNode().ID, edge->symbol->name,
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
