#include "netlist/NetlistGraph.hpp"

#include "NetlistBuilder.hpp"

#include "netlist/DepthFirstSearch.hpp"
#include "netlist/Utilities.hpp"

#include <algorithm>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>

using namespace slang::netlist;

void NetlistGraph::build(ast::Compilation &compilation,
                         analysis::AnalysisManager &analysisManager,
                         BuilderOptions options) {
  NetlistBuilder builder(compilation, analysisManager, *this, options);
  builder.build(compilation.getRoot());
  builder.finalize();
  setBuildProfile(builder.getBuildProfile());
}

void NetlistGraph::buildIndex() const {
  if (indexBuilt)
    return;
  for (auto const &node : nodes) {
    auto path = node->getHierarchicalPath();
    if (path.has_value()) {
      nodeIndex[std::string(*path)].push_back(node.get());
    }
  }
  indexBuilt = true;
}

auto NetlistGraph::lookup(std::string_view name) const -> NetlistNode * {
  buildIndex();
  auto it = nodeIndex.find(std::string(name));
  if (it == nodeIndex.end() || it->second.empty())
    return nullptr;
  return it->second.front();
}

auto NetlistGraph::lookup(std::string_view name, DriverBitRange bounds) const
    -> std::vector<NetlistNode *> {
  buildIndex();
  std::vector<NetlistNode *> result;
  auto it = nodeIndex.find(std::string(name));
  if (it == nodeIndex.end())
    return result;
  for (auto *node : it->second) {
    auto nodeBounds = node->getBounds();
    if (nodeBounds.has_value() && nodeBounds->overlaps(bounds)) {
      result.push_back(node);
    }
  }
  return result;
}

auto NetlistGraph::getDrivers(std::string_view name,
                              DriverBitRange bounds) const
    -> std::vector<NetlistNode *> {
  std::unordered_set<NetlistNode *> seen;
  std::vector<NetlistNode *> result;
  for (auto const &node : nodes) {
    for (auto const &edge : node->getOutEdges()) {
      if (edge->symbol.hierarchicalPath != name) {
        continue;
      }
      if (!edge->bounds.overlaps(bounds)) {
        continue;
      }
      auto *source = &edge->getSourceNode();
      if (seen.insert(source).second) {
        result.push_back(source);
      }
    }
  }
  return result;
}

namespace {

struct CombFanPredicate {
  bool operator()(const NetlistEdge &edge) const {
    return !edge.disabled && edge.getTargetNode().kind != NodeKind::State;
  }
};

struct CombFanBackwardPredicate {
  bool operator()(const NetlistEdge &edge) const {
    return !edge.disabled && edge.getSourceNode().kind != NodeKind::State;
  }
};

class CollectVisitor {
public:
  CollectVisitor(std::vector<NetlistNode *> &result) : result(result) {}
  void visitedNode(NetlistNode &) {}
  void visitNode(NetlistNode &node) { result.push_back(&node); }
  void visitEdge(NetlistEdge &) {}
  void popNode() {}

private:
  std::vector<NetlistNode *> &result;
};

} // namespace

auto NetlistGraph::getCombFanOut(NetlistNode &node) const
    -> std::vector<NetlistNode *> {
  std::vector<NetlistNode *> result;
  CollectVisitor visitor(result);
  DepthFirstSearch<NetlistNode, NetlistEdge, CollectVisitor, CombFanPredicate,
                   Direction::Forward>
      dfs(visitor, node);
  return result;
}

auto NetlistGraph::getCombFanIn(NetlistNode &node) const
    -> std::vector<NetlistNode *> {
  std::vector<NetlistNode *> result;
  CollectVisitor visitor(result);
  DepthFirstSearch<NetlistNode, NetlistEdge, CollectVisitor,
                   CombFanBackwardPredicate, Direction::Backward>
      dfs(visitor, node);
  return result;
}

auto NetlistGraph::getSensitivity(NetlistNode &node) const
    -> std::vector<SensitivitySource> {
  std::vector<SensitivitySource> result;

  auto collectFromState = [&](NetlistNode &state) {
    SLANG_ASSERT(state.kind == NodeKind::State);
    for (auto const &edge : state.getInEdges()) {
      if (edge->disabled || edge->edgeKind == ast::EdgeKind::None) {
        continue;
      }
      auto *source = &edge->getSourceNode();
      auto duplicate =
          std::any_of(result.begin(), result.end(), [&](auto const &existing) {
            return existing.source == source &&
                   existing.edgeKind == edge->edgeKind;
          });
      if (!duplicate) {
        result.push_back({source, edge->edgeKind});
      }
    }
  };

  if (node.kind == NodeKind::State) {
    collectFromState(node);
    return result;
  }

  // Forward walk: collect State targets without traversing into them.
  // getCombFanOut can't be reused — its predicate drops edges-to-State.
  std::unordered_set<NetlistNode *> visited;
  std::vector<NetlistNode *> stack;
  visited.insert(&node);
  stack.push_back(&node);
  while (!stack.empty()) {
    auto *cur = stack.back();
    stack.pop_back();
    for (auto const &edge : cur->getOutEdges()) {
      if (edge->disabled) {
        continue;
      }
      auto &target = edge->getTargetNode();
      if (target.kind == NodeKind::State) {
        collectFromState(target);
        continue;
      }
      if (visited.insert(&target).second) {
        stack.push_back(&target);
      }
    }
  }
  return result;
}

auto NetlistGraph::findNodes(std::string_view pattern) const
    -> std::vector<NetlistNode *> {
  buildIndex();
  std::string pat(pattern);
  std::vector<NetlistNode *> result;
  for (auto const &[name, nodeList] : nodeIndex) {
    if (Utilities::wildcardMatch(name.c_str(), pat.c_str())) {
      result.insert(result.end(), nodeList.begin(), nodeList.end());
    }
  }
  return result;
}

auto NetlistGraph::findNodesRegex(std::string_view pattern) const
    -> std::vector<NetlistNode *> {
  buildIndex();
  std::regex re(pattern.begin(), pattern.end());
  std::vector<NetlistNode *> result;
  for (auto const &[name, nodeList] : nodeIndex) {
    if (std::regex_match(name, re)) {
      result.insert(result.end(), nodeList.begin(), nodeList.end());
    }
  }
  return result;
}
