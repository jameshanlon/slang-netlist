#include "netlist/NetlistSerializer.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <unordered_map>

using json = nlohmann::json;

namespace slang::netlist {

//===----------------------------------------------------------------------===//
// String conversion helpers
//===----------------------------------------------------------------------===//

static auto nodeKindToString(NodeKind kind) -> std::string_view {
  switch (kind) {
  case NodeKind::None:
    return "None";
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
  }
  return "None";
}

static auto nodeKindFromString(std::string_view str) -> NodeKind {
  if (str == "Port") {
    return NodeKind::Port;
  }
  if (str == "Variable") {
    return NodeKind::Variable;
  }
  if (str == "Assignment") {
    return NodeKind::Assignment;
  }
  if (str == "Conditional") {
    return NodeKind::Conditional;
  }
  if (str == "Case") {
    return NodeKind::Case;
  }
  if (str == "Merge") {
    return NodeKind::Merge;
  }
  if (str == "State") {
    return NodeKind::State;
  }
  return NodeKind::None;
}

static auto edgeKindToString(ast::EdgeKind kind) -> std::string_view {
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
  return "None";
}

static auto edgeKindFromString(std::string_view str) -> ast::EdgeKind {
  if (str == "PosEdge") {
    return ast::EdgeKind::PosEdge;
  }
  if (str == "NegEdge") {
    return ast::EdgeKind::NegEdge;
  }
  if (str == "BothEdges") {
    return ast::EdgeKind::BothEdges;
  }
  return ast::EdgeKind::None;
}

static auto directionToString(ast::ArgumentDirection dir) -> std::string_view {
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
  return "In";
}

static auto directionFromString(std::string_view str)
    -> ast::ArgumentDirection {
  if (str == "Out") {
    return ast::ArgumentDirection::Out;
  }
  if (str == "InOut") {
    return ast::ArgumentDirection::InOut;
  }
  if (str == "Ref") {
    return ast::ArgumentDirection::Ref;
  }
  return ast::ArgumentDirection::In;
}

//===----------------------------------------------------------------------===//
// JSON helpers
//===----------------------------------------------------------------------===//

static auto locationToJson(TextLocation const &loc) -> json {
  return {
      {"fileIndex", loc.fileIndex}, {"line", loc.line}, {"column", loc.column}};
}

static auto locationFromJson(json const &j) -> TextLocation {
  return {j.at("fileIndex").get<uint32_t>(), j.at("line").get<size_t>(),
          j.at("column").get<size_t>()};
}

static auto symbolToJson(SymbolReference const &sym) -> json {
  json j;
  j["name"] = sym.name;
  j["path"] = sym.hierarchicalPath;
  j["location"] = locationToJson(sym.location);
  return j;
}

static auto symbolFromJson(json const &j) -> SymbolReference {
  return {j.at("name").get<std::string>(), j.at("path").get<std::string>(),
          locationFromJson(j.at("location"))};
}

//===----------------------------------------------------------------------===//
// Serialize
//===----------------------------------------------------------------------===//

auto NetlistSerializer::serialize(NetlistGraph const &graph) -> std::string {
  json root;
  root["version"] = formatVersion;

  // Serialize file table.
  json fileTableJson = json::array();
  for (size_t i = 0; i < graph.fileTable.size(); ++i) {
    fileTableJson.push_back(
        std::string(graph.fileTable.getFilename(static_cast<uint32_t>(i))));
  }
  root["fileTable"] = fileTableJson;

  // Serialize nodes.
  json nodesJson = json::array();
  for (auto const &nodePtr : graph) {
    auto const &node = *nodePtr;
    json nodeJson;
    nodeJson["id"] = node.ID;
    nodeJson["kind"] = nodeKindToString(node.kind);

    switch (node.kind) {
    case NodeKind::Port: {
      auto const &port = node.as<Port>();
      nodeJson["path"] = port.hierarchicalPath;
      nodeJson["name"] = port.name;
      nodeJson["bounds"] = {port.bounds.lower(), port.bounds.upper()};
      nodeJson["direction"] = directionToString(port.direction);
      nodeJson["location"] = locationToJson(port.location);
      break;
    }
    case NodeKind::Variable: {
      auto const &var = node.as<Variable>();
      nodeJson["path"] = var.hierarchicalPath;
      nodeJson["name"] = var.name;
      nodeJson["bounds"] = {var.bounds.lower(), var.bounds.upper()};
      nodeJson["location"] = locationToJson(var.location);
      break;
    }
    case NodeKind::State: {
      auto const &state = node.as<State>();
      nodeJson["path"] = state.hierarchicalPath;
      nodeJson["name"] = state.name;
      nodeJson["bounds"] = {state.bounds.lower(), state.bounds.upper()};
      nodeJson["location"] = locationToJson(state.location);
      break;
    }
    case NodeKind::Assignment: {
      auto const &assign = node.as<Assignment>();
      nodeJson["location"] = locationToJson(assign.location);
      break;
    }
    case NodeKind::Conditional: {
      auto const &cond = node.as<Conditional>();
      nodeJson["location"] = locationToJson(cond.location);
      break;
    }
    case NodeKind::Case: {
      auto const &caseNode = node.as<Case>();
      nodeJson["location"] = locationToJson(caseNode.location);
      break;
    }
    case NodeKind::Merge:
    case NodeKind::None:
      break;
    }

    nodesJson.push_back(std::move(nodeJson));
  }
  root["nodes"] = nodesJson;

  // Serialize edges.
  json edgesJson = json::array();
  for (auto const &nodePtr : graph) {
    for (auto const &edgePtr : nodePtr->getOutEdges()) {
      auto const &edge = *edgePtr;
      json edgeJson;
      edgeJson["source"] = edge.getSourceNode().ID;
      edgeJson["target"] = edge.getTargetNode().ID;
      edgeJson["edgeKind"] = edgeKindToString(edge.edgeKind);
      edgeJson["symbol"] = symbolToJson(edge.symbol);
      edgeJson["bounds"] = {edge.bounds.lower(), edge.bounds.upper()};
      edgeJson["disabled"] = edge.disabled;
      edgesJson.push_back(std::move(edgeJson));
    }
  }
  root["edges"] = edgesJson;

  return root.dump(2);
}

//===----------------------------------------------------------------------===//
// Deserialize
//===----------------------------------------------------------------------===//

void NetlistSerializer::deserialize(std::string_view jsonStr,
                                    NetlistGraph &graph) {
  auto root = json::parse(jsonStr);

  auto version = root.at("version").get<int>();
  if (version != formatVersion) {
    throw std::runtime_error("unsupported netlist format version: " +
                             std::to_string(version));
  }

  // Deserialize file table.
  for (auto const &entry : root.at("fileTable")) {
    graph.fileTable.addFile(entry.get<std::string>());
  }

  // Deserialize nodes.
  std::unordered_map<size_t, NetlistNode *> idMap;

  for (auto const &nodeJson : root.at("nodes")) {
    auto id = nodeJson.at("id").get<size_t>();
    auto kind = nodeKindFromString(nodeJson.at("kind").get<std::string>());

    std::unique_ptr<NetlistNode> node;

    switch (kind) {
    case NodeKind::Port: {
      auto boundsArr = nodeJson.at("bounds");
      auto portNode = std::make_unique<Port>(
          nodeJson.at("name").get<std::string>(),
          nodeJson.at("path").get<std::string>(),
          locationFromJson(nodeJson.at("location")),
          directionFromString(nodeJson.at("direction").get<std::string>()),
          DriverBitRange{boundsArr[0].get<int32_t>(),
                         boundsArr[1].get<int32_t>()});
      node = std::move(portNode);
      break;
    }
    case NodeKind::Variable: {
      auto boundsArr = nodeJson.at("bounds");
      auto varNode = std::make_unique<Variable>(
          nodeJson.at("name").get<std::string>(),
          nodeJson.at("path").get<std::string>(),
          locationFromJson(nodeJson.at("location")),
          DriverBitRange{boundsArr[0].get<int32_t>(),
                         boundsArr[1].get<int32_t>()});
      node = std::move(varNode);
      break;
    }
    case NodeKind::State: {
      auto boundsArr = nodeJson.at("bounds");
      auto stateNode =
          std::make_unique<State>(nodeJson.at("name").get<std::string>(),
                                  nodeJson.at("path").get<std::string>(),
                                  locationFromJson(nodeJson.at("location")),
                                  DriverBitRange{boundsArr[0].get<int32_t>(),
                                                 boundsArr[1].get<int32_t>()});
      node = std::move(stateNode);
      break;
    }
    case NodeKind::Assignment: {
      node = std::make_unique<Assignment>(
          locationFromJson(nodeJson.at("location")));
      break;
    }
    case NodeKind::Conditional: {
      node = std::make_unique<Conditional>(
          locationFromJson(nodeJson.at("location")));
      break;
    }
    case NodeKind::Case: {
      node = std::make_unique<Case>(locationFromJson(nodeJson.at("location")));
      break;
    }
    case NodeKind::Merge: {
      node = std::make_unique<Merge>();
      break;
    }
    case NodeKind::None: {
      node = std::make_unique<NetlistNode>(NodeKind::None);
      break;
    }
    }

    auto &addedNode = graph.addNode(std::move(node));
    idMap[id] = &addedNode;
  }

  // Deserialize edges.
  for (auto const &edgeJson : root.at("edges")) {
    auto sourceId = edgeJson.at("source").get<size_t>();
    auto targetId = edgeJson.at("target").get<size_t>();

    auto sourceIt = idMap.find(sourceId);
    auto targetIt = idMap.find(targetId);
    if (sourceIt == idMap.end() || targetIt == idMap.end()) {
      throw std::runtime_error("edge references unknown node ID");
    }

    auto &edge = graph.addEdge(*sourceIt->second, *targetIt->second);
    edge.edgeKind =
        edgeKindFromString(edgeJson.at("edgeKind").get<std::string>());
    edge.symbol = symbolFromJson(edgeJson.at("symbol"));
    auto boundsArr = edgeJson.at("bounds");
    edge.bounds = DriverBitRange{boundsArr[0].get<int32_t>(),
                                 boundsArr[1].get<int32_t>()};
    edge.disabled = edgeJson.at("disabled").get<bool>();
  }
}

} // namespace slang::netlist
