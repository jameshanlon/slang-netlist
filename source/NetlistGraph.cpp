#include "netlist/NetlistGraph.hpp"

namespace slang::netlist {

NetlistGraph::NetlistGraph() : mapAllocator(allocator) {
  NetlistNode::nextID = 0; // Reset the static ID counter.
}

void NetlistGraph::finalize() { processPendingRvalues(); }

NetlistNode *NetlistGraph::lookupDriver(ast::ValueSymbol const &symbol,
                                        std::pair<uint64_t, uint64_t> bounds) {
  if (symbolToSlot.contains(&symbol)) {
    auto &map = driverMap[symbolToSlot[&symbol]];
    for (auto it = map.find(bounds); it != map.end(); it++) {
      if (ConstantRange(it.bounds()).contains(ConstantRange(bounds))) {
        return *it;
      }
    }
  }
  return nullptr;
}

void NetlistGraph::addRvalue(const ast::ValueSymbol *symbol,
                             std::pair<uint64_t, uint64_t> bounds,
                             NetlistNode *node) {
  DEBUG_PRINT("Adding pending R-value: {} [{}:{}]\n", symbol->name,
              bounds.first, bounds.second);
  SLANG_ASSERT(symbol != nullptr && "Symbol must not be null");
  pendingRValues.emplace_back(symbol, bounds, node);
}

void NetlistGraph::processPendingRvalues() {
  for (auto &pending : pendingRValues) {
    DEBUG_PRINT("Processing pending R-value: {} [{}:{}]\n",
                pending.symbol->name, pending.bounds.first,
                pending.bounds.second);
    if (pending.node) {
      if (symbolToSlot.contains(pending.symbol)) {
        auto &map = driverMap[symbolToSlot[pending.symbol]];
        for (auto it = map.find(pending.bounds); it != map.end(); it++) {
          auto &edge = addEdge(**it, *pending.node);
          edge.setVariable(pending.symbol, pending.bounds);
          DEBUG_PRINT("  Added edge from driver node {} to R-value node {}\n",
                      (*it)->ID, pending.node->ID);
        }
      }
    }
  }
  pendingRValues.clear();
}

void NetlistGraph::mergeDrivers(
    SymbolSlotMap const &procSymbolToSlot,
    std::vector<SymbolDriverMap> const &procDriverMap, ast::EdgeKind edgeKind) {
  for (auto [symbol, index] : procSymbolToSlot) {
    DEBUG_PRINT("Merging drivers for symbol {} at proc index {}\n",
                symbol->name, index);
    auto [it, inserted] =
        symbolToSlot.try_emplace(symbol, (uint32_t)symbolToSlot.size());
    auto globalIndex = it->second;
    if (globalIndex >= driverMap.size()) {
      driverMap.emplace_back();
    }
    DEBUG_PRINT("Merging drivers into global map: symbol {} at proc index {} "
                "global index {}\n",
                symbol->name, index, globalIndex);
    if (procDriverMap.empty()) {
      continue;
    }
    for (auto it = procDriverMap[index].begin();
         it != procDriverMap[index].end(); it++) {
      DEBUG_PRINT("  Merging driver interval: [{}:{}]\n", it.bounds().first,
                  it.bounds().second);
      NetlistNode *node = nullptr;
      if (edgeKind == ast::EdgeKind::None) {
        driverMap[globalIndex].insert(it.bounds(), *it, mapAllocator);
        node = *it;
      } else {
        node = lookupDriver(*symbol, it.bounds());
        if (node) {
          addEdge(**it, *node).setVariable(symbol, it.bounds());
        } else {
          node = &addNode(std::make_unique<State>(symbol, it.bounds()));
          addEdge(**it, *node).setVariable(symbol, it.bounds());
          driverMap[globalIndex].insert(it.bounds(), node, mapAllocator);
        }
      }
      if (portMap.contains(symbol) && portMap[symbol]->isOutput()) {
        DEBUG_PRINT("Adding port dependency for symbol {} to port {}\n",
                    symbol->name, portMap[symbol]->internalSymbol->name);
        auto &edge = addEdge(*node, *portMap[symbol]);
        edge.setVariable(symbol, it.bounds());
      }
    }
  }
}

void NetlistGraph::handleLvalue(const ast::ValueSymbol &symbol,
                                std::pair<uint32_t, uint32_t> bounds,
                                NetlistNode *node) {
  DEBUG_PRINT("Handle global lvalue: {} [{}:{}]\n", symbol.name, bounds.first,
              bounds.second);
  auto [it, inserted] =
      symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());
  auto index = it->second;
  if (index >= driverMap.size()) {
    driverMap.emplace_back();
  }
  driverMap[index].insert(bounds, node, mapAllocator);
}

void NetlistGraph::addPort(ast::PortSymbol const &symbol) {
  auto &node =
      addNode(std::make_unique<Port>(symbol.direction, symbol.internalSymbol));
  portMap[symbol.internalSymbol] = &node.as<Port>();
}

std::optional<NetlistNode *> NetlistGraph::getPort(ast::Symbol const *symbol) {
  if (portMap.contains(symbol)) {
    return portMap[symbol];
  }
  return std::nullopt;
}

void NetlistGraph::connectInputPort(ast::ValueSymbol const &symbol,
                                    std::pair<uint64_t, uint64_t> bounds) {
  handleLvalue(symbol, bounds, portMap[&symbol]);
}

NetlistNode *NetlistGraph::lookup(std::string_view name) const {
  auto compare = [&](const std::unique_ptr<NetlistNode> &node) {
    switch (node->kind) {
    case NodeKind::Port:
      return node->as<Port>().internalSymbol->getHierarchicalPath() == name;
    default:
      return false;
    }
  };
  auto it = std::ranges::find_if(*this, compare);
  return it != this->end() ? it->get() : nullptr;
}

} // namespace slang::netlist
