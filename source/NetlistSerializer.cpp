#include "netlist/NetlistSerializer.hpp"

#include "slang/util/Util.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <unordered_map>

using json = nlohmann::json;

namespace slang::netlist {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

auto nodeKindToString(NodeKind kind) -> std::string_view {
  switch (kind) {
  case NodeKind::Port:
    return "Port";
  case NodeKind::Variable:
    return "Variable";
  case NodeKind::Assignment:
    return "Assignment";
  case NodeKind::Conditional:
    return "Conditional";
  case NodeKind::Case:
    return "Case";
  case NodeKind::Merge:
    return "Merge";
  case NodeKind::State:
    return "State";
  default:
    return "None";
  }
}

auto nodeKindFromString(std::string_view s) -> NodeKind {
  if (s == "Port")
    return NodeKind::Port;
  if (s == "Variable")
    return NodeKind::Variable;
  if (s == "Assignment")
    return NodeKind::Assignment;
  if (s == "Conditional")
    return NodeKind::Conditional;
  if (s == "Case")
    return NodeKind::Case;
  if (s == "Merge")
    return NodeKind::Merge;
  if (s == "State")
    return NodeKind::State;
  return NodeKind::None;
}

auto edgeKindToString(ast::EdgeKind kind) -> std::string_view {
  switch (kind) {
  case ast::EdgeKind::None:
    return "None";
  case ast::EdgeKind::PosEdge:
    return "PosEdge";
  case ast::EdgeKind::NegEdge:
    return "NegEdge";
  case ast::EdgeKind::BothEdges:
    return "BothEdges";
  }
  SLANG_UNREACHABLE;
}

auto edgeKindFromString(std::string_view s) -> ast::EdgeKind {
  if (s == "PosEdge")
    return ast::EdgeKind::PosEdge;
  if (s == "NegEdge")
    return ast::EdgeKind::NegEdge;
  if (s == "BothEdges")
    return ast::EdgeKind::BothEdges;
  return ast::EdgeKind::None;
}

auto directionToString(ast::ArgumentDirection dir) -> std::string_view {
  switch (dir) {
  case ast::ArgumentDirection::In:
    return "In";
  case ast::ArgumentDirection::Out:
    return "Out";
  case ast::ArgumentDirection::InOut:
    return "InOut";
  case ast::ArgumentDirection::Ref:
    return "Ref";
  }
  SLANG_UNREACHABLE;
}

auto directionFromString(std::string_view s) -> ast::ArgumentDirection {
  if (s == "Out")
    return ast::ArgumentDirection::Out;
  if (s == "InOut")
    return ast::ArgumentDirection::InOut;
  if (s == "Ref")
    return ast::ArgumentDirection::Ref;
  return ast::ArgumentDirection::In;
}

} // namespace

// ---------------------------------------------------------------------------
// NetlistSerializer::serialize
// ---------------------------------------------------------------------------

auto NetlistSerializer::serialize(NetlistGraph const &netlist) -> std::string {
  json j;
  j["version"] = formatVersion;

  json nodes = json::array();
  for (auto const &node : netlist) {
    json n;
    n["id"] = node->ID;
    n["kind"] = nodeKindToString(node->kind);

    switch (node->kind) {
    case NodeKind::Port: {
      auto const &port = node->as<Port>();
      SLANG_ASSERT(port.symbol.internalSymbol != nullptr);
      n["path"] = port.symbol.internalSymbol->getHierarchicalPath();
      n["name"] = std::string(port.symbol.name);
      n["bounds"] = {port.bounds.left, port.bounds.right};
      n["direction"] = directionToString(port.symbol.direction);
      break;
    }
    case NodeKind::Variable: {
      auto const &var = node->as<Variable>();
      n["path"] = var.symbol.getHierarchicalPath();
      n["name"] = std::string(var.symbol.name);
      n["bounds"] = {var.bounds.left, var.bounds.right};
      break;
    }
    case NodeKind::State: {
      auto const &state = node->as<State>();
      n["path"] = state.symbol.getHierarchicalPath();
      n["name"] = std::string(state.symbol.name);
      n["bounds"] = {state.bounds.left, state.bounds.right};
      break;
    }
    default:
      // Assignment, Conditional, Case, Merge: no symbol data to extract.
      break;
    }

    nodes.push_back(std::move(n));
  }
  j["nodes"] = std::move(nodes);

  json edges = json::array();
  for (auto const &node : netlist) {
    for (auto const &edge : node->getOutEdges()) {
      json e;
      e["source"] = node->ID;
      e["target"] = edge->getTargetNode().ID;
      e["edgeKind"] = edgeKindToString(edge->edgeKind);
      e["disabled"] = edge->disabled;
      if (edge->symbol) {
        e["symbol"] = std::string(edge->symbol->name);
        e["bounds"] = {edge->bounds.left, edge->bounds.right};
      }
      edges.push_back(std::move(e));
    }
  }
  j["edges"] = std::move(edges);

  return j.dump(2);
}

// ---------------------------------------------------------------------------
// NetlistSerializer::deserialize
// ---------------------------------------------------------------------------

auto NetlistSerializer::deserialize(std::string_view json_str)
    -> FlatNetlistGraph {
  json j = json::parse(json_str);

  int version = j.at("version").get<int>();
  if (version != formatVersion) {
    throw std::runtime_error(
        fmt::format("unsupported netlist JSON version: {}", version));
  }

  FlatNetlistGraph graph;
  std::unordered_map<size_t, FlatNetlistNode *> idToNode;

  for (auto const &n : j.at("nodes")) {
    auto id = n.at("id").get<size_t>();
    auto kind = nodeKindFromString(n.at("kind").get<std::string>());

    auto node = std::make_unique<FlatNetlistNode>(id, kind);

    if (n.contains("path")) {
      node->hierarchicalPath = n.at("path").get<std::string>();
    }
    if (n.contains("name")) {
      node->name = n.at("name").get<std::string>();
    }
    if (n.contains("bounds")) {
      auto const &b = n.at("bounds");
      node->bounds = {b[0].get<int32_t>(), b[1].get<int32_t>()};
    }
    if (n.contains("direction")) {
      node->direction =
          directionFromString(n.at("direction").get<std::string>());
    }

    auto *ptr = &graph.addNode(std::move(node));
    idToNode[id] = ptr;
  }

  for (auto const &e : j.at("edges")) {
    auto sourceId = e.at("source").get<size_t>();
    auto targetId = e.at("target").get<size_t>();

    auto *sourceNode = idToNode.at(sourceId);
    auto *targetNode = idToNode.at(targetId);

    auto &edge = graph.addEdge(*sourceNode, *targetNode);
    edge.edgeKind = edgeKindFromString(e.at("edgeKind").get<std::string>());
    edge.disabled = e.at("disabled").get<bool>();
    if (e.contains("symbol")) {
      edge.symbolName = e.at("symbol").get<std::string>();
      auto const &b = e.at("bounds");
      edge.bounds = {b[0].get<int32_t>(), b[1].get<int32_t>()};
    }
  }

  return graph;
}

} // namespace slang::netlist
