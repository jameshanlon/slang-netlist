#include "netlist/NetlistGraph.hpp"

#include "NetlistBuilder.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

using namespace slang::netlist;

void NetlistGraph::build(ast::Compilation &compilation,
                         analysis::AnalysisManager &analysisManager,
                         bool parallel, unsigned numThreads) {
  NetlistBuilder builder(compilation, analysisManager, *this);
  builder.build(compilation.getRoot(), parallel, numThreads);
  builder.finalize();
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
