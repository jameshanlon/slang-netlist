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

static auto assignmentTypeToString(AssignmentType type) -> std::string_view {
  switch (type) {
  case AssignmentType::Continuous:
    return "assign";
  case AssignmentType::Initial:
    return "initial";
  case AssignmentType::Final:
    return "final";
  case AssignmentType::Always:
    return "always";
  case AssignmentType::AlwaysComb:
    return "always_comb";
  case AssignmentType::AlwaysLatch:
    return "always_latch";
  case AssignmentType::AlwaysFF:
    return "always_ff";
  case AssignmentType::Procedural:
    return "procedural";
  }
  return "procedural";
}

static auto assignmentTypeFromString(std::string_view str) -> AssignmentType {
  if (str == "assign") {
    return AssignmentType::Continuous;
  }
  if (str == "initial") {
    return AssignmentType::Initial;
  }
  if (str == "final") {
    return AssignmentType::Final;
  }
  if (str == "always") {
    return AssignmentType::Always;
  }
  if (str == "always_comb") {
    return AssignmentType::AlwaysComb;
  }
  if (str == "always_latch") {
    return AssignmentType::AlwaysLatch;
  }
  if (str == "always_ff") {
    return AssignmentType::AlwaysFF;
  }
  return AssignmentType::Procedural;
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

static auto branchPredicateToJson(BranchPredicate const &predicate) -> json {
  json predicateJson;

  if (!predicate.op.empty()) {
    predicateJson["op"] = predicate.op;
  }
  if (!predicate.kind.empty()) {
    predicateJson["kind"] = predicate.kind;
  }
  if (!predicate.signal.empty()) {
    predicateJson["signal"] = predicate.signal;
  }
  if (predicate.bitWidth != 0) {
    predicateJson["bit_width"] = predicate.bitWidth;
  }
  if (predicate.bounds) {
    predicateJson["bounds"] = {predicate.bounds->lower(),
                               predicate.bounds->upper()};
  }
  if (!predicate.values.empty()) {
    predicateJson["values"] = predicate.values;
  }
  if (!predicate.value.empty()) {
    predicateJson["value"] = predicate.value;
  }
  if (!predicate.mask.empty()) {
    predicateJson["mask"] = predicate.mask;
  }
  if (!predicate.excluded.empty()) {
    json excludedJson = json::array();
    for (auto const &pattern : predicate.excluded) {
      excludedJson.push_back(
          {{"value", pattern.value}, {"mask", pattern.mask}});
    }
    predicateJson["excluded"] = std::move(excludedJson);
  }
  if (!predicate.source.empty()) {
    predicateJson["source"] = locationToJson(predicate.source);
  }
  if (!predicate.expression.empty()) {
    predicateJson["expression"] = predicate.expression;
  }
  if (!predicate.terms.empty()) {
    json termsJson = json::array();
    for (auto const &term : predicate.terms) {
      termsJson.push_back(branchPredicateToJson(term));
    }
    predicateJson["terms"] = std::move(termsJson);
  }

  return predicateJson;
}

static auto branchPredicateFromJson(json const &predicateJson)
    -> BranchPredicate {
  BranchPredicate predicate;

  if (predicateJson.contains("op")) {
    predicate.op = predicateJson.at("op").get<std::string>();
  }
  if (predicateJson.contains("kind")) {
    predicate.kind = predicateJson.at("kind").get<std::string>();
  }
  if (predicateJson.contains("signal")) {
    predicate.signal = predicateJson.at("signal").get<std::string>();
  }
  if (predicateJson.contains("bit_width")) {
    predicate.bitWidth = predicateJson.at("bit_width").get<uint64_t>();
  }
  if (predicateJson.contains("bounds")) {
    auto boundsArr = predicateJson.at("bounds");
    predicate.bounds = DriverBitRange{boundsArr[0].get<int32_t>(),
                                      boundsArr[1].get<int32_t>()};
  }
  if (predicateJson.contains("values")) {
    predicate.values =
        predicateJson.at("values").get<std::vector<std::string>>();
  }
  if (predicateJson.contains("value")) {
    predicate.value = predicateJson.at("value").get<std::string>();
  }
  if (predicateJson.contains("mask")) {
    predicate.mask = predicateJson.at("mask").get<std::string>();
  }
  if (predicateJson.contains("excluded")) {
    for (auto const &patternJson : predicateJson.at("excluded")) {
      predicate.excluded.push_back(
          {patternJson.at("value").get<std::string>(),
           patternJson.at("mask").get<std::string>()});
    }
  }
  if (predicateJson.contains("source")) {
    predicate.source = locationFromJson(predicateJson.at("source"));
  }
  if (predicateJson.contains("expression")) {
    predicate.expression = predicateJson.at("expression").get<std::string>();
  }
  if (predicateJson.contains("terms")) {
    for (auto const &termJson : predicateJson.at("terms")) {
      predicate.terms.push_back(branchPredicateFromJson(termJson));
    }
  }

  return predicate;
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
      nodeJson["assignmentType"] =
          assignmentTypeToString(assign.assignmentType);
      nodeJson["isBlocking"] = assign.isBlocking;
      if (assign.branchPredicate) {
        nodeJson["branchPredicate"] =
            branchPredicateToJson(*assign.branchPredicate);
      }
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
      if (edge.branchPredicate) {
        edgeJson["branchPredicate"] =
            branchPredicateToJson(*edge.branchPredicate);
      }
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
  json root;
  try {
    root = json::parse(jsonStr);
  } catch (json::parse_error const &e) {
    throw std::runtime_error(std::string("JSON parse error: ") + e.what());
  }

  auto version = root.at("version").get<int>();
  if (version != 2 && version != 3 && version != formatVersion) {
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
      auto assignmentType = AssignmentType::Procedural;
      if (nodeJson.contains("assignmentType")) {
        assignmentType = assignmentTypeFromString(
            nodeJson.at("assignmentType").get<std::string>());
      }
      auto isBlocking = nodeJson.contains("isBlocking")
                            ? nodeJson.at("isBlocking").get<bool>()
                            : false;
      auto assignNode = std::make_unique<Assignment>(
          locationFromJson(nodeJson.at("location")), assignmentType,
          isBlocking);
      if (nodeJson.contains("branchPredicate")) {
        assignNode->branchPredicate =
            branchPredicateFromJson(nodeJson.at("branchPredicate"));
      }
      node = std::move(assignNode);
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
    if (edgeJson.contains("branchPredicate")) {
      edge.branchPredicate =
          branchPredicateFromJson(edgeJson.at("branchPredicate"));
    }
  }
}

} // namespace slang::netlist
