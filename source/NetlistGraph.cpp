#include "netlist/NetlistGraph.hpp"

namespace slang::netlist {

NetlistGraph::NetlistGraph() : mapAllocator(allocator) {
  NetlistNode::nextID = 0; // Reset the static ID counter.
}

void NetlistGraph::finalize() { processPendingRvalues(); }

std::optional<DriverInfo>
NetlistGraph::getFirstDriver(ast::Symbol const &symbol,
                             std::pair<uint64_t, uint64_t> bounds) {
  if (symbolToSlot.contains(&symbol)) {
    auto &map = driverMap[symbolToSlot[&symbol]];
    for (auto it = map.find(bounds); it != map.end(); it++) {
      if (ConstantRange(it.bounds()).contains(ConstantRange(bounds))) {
        return *it;
      }
    }
  }
  return std::nullopt;
}

std::vector<DriverInfo>
NetlistGraph::getDrivers(ast::Symbol const &symbol,
                         std::pair<uint64_t, uint64_t> bounds) {
  std::vector<DriverInfo> result;
  if (symbolToSlot.contains(&symbol)) {
    auto &map = driverMap[symbolToSlot[&symbol]];
    for (auto it = map.find(bounds); it != map.end(); it++) {
      if (ConstantRange(bounds).contains(ConstantRange(it.bounds()))) {
        result.push_back(*it);
      }
    }
  }
  return result;
}

void NetlistGraph::addRvalue(ast::ValueSymbol const &symbol,
                             ast::Expression const &lsp,
                             std::pair<uint64_t, uint64_t> bounds,
                             NetlistNode *node) {
  DEBUG_PRINT("Adding pending R-value: {} [{}:{}]\n", symbol.name, bounds.first,
              bounds.second);
  pendingRValues.emplace_back(&symbol, &lsp, bounds, node);
}

void NetlistGraph::processPendingRvalues() {
  for (auto &pending : pendingRValues) {
    DEBUG_PRINT("Processing pending R-value: {} [{}:{}]\n",
                pending.symbol->name, pending.bounds.first,
                pending.bounds.second);
    if (pending.node) {

      // Find drivers of the pending R-value, and for each one add edges from
      // the driver to the R-value.
      if (symbolToSlot.contains(pending.symbol)) {
        auto &map = driverMap[symbolToSlot[pending.symbol]];
        for (auto it = map.find(pending.bounds); it != map.end(); it++) {
          auto &edge = addEdge(*(*it).node, *pending.node);
          edge.setVariable(pending.symbol, pending.bounds);
          DEBUG_PRINT("  Added edge from driver node {} to R-value node {}\n",
                      (*it).node->ID, pending.node->ID);
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

    // Create or retrieve symbol index.
    auto [it, inserted] =
        symbolToSlot.try_emplace(symbol, (uint32_t)symbolToSlot.size());

    // Extend driverMap if necessary.
    auto globalIndex = it->second;
    if (globalIndex >= driverMap.size()) {
      driverMap.emplace_back();
    }

    DEBUG_PRINT("Merging drivers into global map: symbol {} at proc index {} "
                "global index {}\n",
                symbol->name, index, globalIndex);

    if (procDriverMap.empty()) {
      // If the procedure driver map is empty, we don't need to do anything.
      continue;
    }

    // Add all the procedure driver intervals to the global map.
    for (auto it = procDriverMap[index].begin();
         it != procDriverMap[index].end(); it++) {

      DEBUG_PRINT("Merging driver interval: [{}:{}]\n", it.bounds().first,
                  it.bounds().second);

      NetlistNode *node = nullptr;

      if (edgeKind == ast::EdgeKind::None) {

        // Combinatorial edge, just add the interval with the driving node.
        driverMap[globalIndex].insert(it.bounds(), *it, mapAllocator);
        node = (*it).node;
      } else {

        // Sequential edge.
        auto driver = getFirstDriver(*symbol, it.bounds());
        if (driver) {

          // If a driver node exists, add an edge from the driver node to the
          // sequential node.
          node = (*it).node;
          addEdge(*node, *driver->node).setVariable(symbol, it.bounds());
        } else {

          // If no driver node exists, create a new sequential node and add
          // the interval with this node.
          node = &addNode(std::make_unique<State>(
              &symbol->as<ast::ValueSymbol>(), it.bounds()));
          addEdge(*(*it).node, *node).setVariable(symbol, it.bounds());
          driverMap[globalIndex].insert(it.bounds(), {node, nullptr},
                                        mapAllocator);
        }
      }

      // TODO: catch hierarchical references for interface hookup.

      // If there is an output port associated with this symbol, then add a
      // dependency from the driver to the port.
      if (auto *portBackRef =
              symbol->as<ast::ValueSymbol>().getFirstPortBackref()) {

        if (portBackRef->getNextBackreference()) {
          DEBUG_PRINT("Ignoring symbol with multiple port back refs");
          return;
        }

        const ast::PortSymbol *portSymbol = portBackRef->port;
        if (auto driver = getFirstDriver(*portSymbol, it.bounds())) {

          DEBUG_PRINT("Adding port dependency for symbol {} to port {}\n",
                      symbol->name, portSymbol->name);

          auto &edge = addEdge(*node, *driver->node);
          edge.setVariable(symbol, it.bounds());
        }
      }
    }
  }
}

void NetlistGraph::addDriver(const ast::Symbol &symbol,
                             const ast::Expression *lsp,
                             std::pair<uint64_t, uint64_t> bounds,
                             NetlistNode *node) {
  DEBUG_PRINT("Add driver: {} {} [{}:{}]\n", toString(symbol.kind), symbol.name,
              bounds.first, bounds.second);

  // Update visited symbols to slots.
  auto [it, inserted] =
      symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());

  // Update current state definitions.
  auto index = it->second;
  if (index >= driverMap.size()) {
    driverMap.emplace_back();
  }

  driverMap[index].insert(bounds, {node, lsp}, mapAllocator);
}

auto NetlistGraph::addPort(const ast::PortSymbol &symbol,
                           std::pair<uint64_t, uint64_t> bounds)
    -> NetlistNode & {
  auto &node =
      addNode(std::make_unique<Port>(symbol.direction, symbol.internalSymbol));
  addDriver(symbol, nullptr, bounds, &node);
  return node;
}

auto NetlistGraph::addModport(ast::ModportPortSymbol const &symbol,
                              std::pair<uint64_t, uint64_t> bounds)
    -> NetlistNode & {
  auto &node = addNode(std::make_unique<Modport>());
  addDriver(symbol, nullptr, bounds, &node);
  return node;
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
