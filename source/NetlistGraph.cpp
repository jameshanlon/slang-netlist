#include "netlist/NetlistGraph.hpp"

namespace slang::netlist {

NetlistGraph::NetlistGraph() {
  NetlistNode::nextID = 0; // Reset the static ID counter.
}

void NetlistGraph::finalize() { processPendingRvalues(); }

void NetlistGraph::addRvalue(ast::ValueSymbol const &symbol,
                             ast::Expression const &lsp, DriverBitRange bounds,
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
      auto driverList =
          driverMap.getDrivers(drivers, *pending.symbol, pending.bounds);
      for (auto &source : driverList) {
        auto &edge = addEdge(*source.node, *pending.node);
        edge.setVariable(pending.symbol, pending.bounds);
        DEBUG_PRINT("Added edge from driver node {} to R-value node {}\n",
                    source.node->ID, pending.node->ID);
      }
    }
  }
  pendingRValues.clear();
}

void NetlistGraph::hookupOutputPort(ast::ValueSymbol const &symbol,
                                    DriverBitRange bounds,
                                    DriverList const &driverList) {

  // If there is an output port associated with this symbol, then add a
  // dependency from the driver to the port.
  if (auto *portBackRef = symbol.getFirstPortBackref()) {

    if (portBackRef->getNextBackreference()) {
      DEBUG_PRINT("Ignoring symbol with multiple port back refs");
      return;
    }

    // Lookup the port node(s) in the graph.
    const ast::PortSymbol *portSymbol = portBackRef->port;
    auto portNodes = getDrivers(*portSymbol, bounds);

    // Connect the drivers to the port node(s).
    for (auto &driver : driverList) {

      for (auto &portDriver : portNodes) {
        auto *portNode = portDriver.node;
        SLANG_ASSERT(portNode->kind == NodeKind::Port);
        addEdge(*driver.node, *portNode).setVariable(&symbol, bounds);

        DEBUG_PRINT("Adding port dependency for symbol {} to port {}\n",
                    symbol.name, portSymbol->name);
      }
    }
  }
}

void NetlistGraph::mergeProcDrivers(SymbolTracker const &symbolTracker,
                                    SymbolDrivers const &symbolDrivers,
                                    ast::EdgeKind edgeKind) {
  DEBUG_PRINT("Merging procedural drivers\n");

  for (auto [symbol, index] : symbolTracker) {
    DEBUG_PRINT("Symbol {} at index={}\n", symbol->name, index);

    if (symbolDrivers[index].empty()) {
      // No drivers for this symbol so we don't need to do anything.
      continue;
    }

    // Merge all of the driver intervals for the symbol into the global map.
    for (auto it = symbolDrivers[index].begin();
         it != symbolDrivers[index].end(); it++) {

      DEBUG_PRINT("Merging driver interval: [{}:{}]\n", it.bounds().first,
                  it.bounds().second);

      auto &driverList = symbolDrivers[index].getDriverList(*it);

      if (edgeKind == ast::EdgeKind::None) {

        // Combinatorial edge, so just add the interval with the driving
        // node(s).

        mergeDrivers(*symbol, it.bounds(), driverList);

        hookupOutputPort(symbol->as<ast::ValueSymbol>(), it.bounds(),
                         driverList);

      } else {

        // Sequential edge, so the procedural drivers act on a stateful variable
        // which is represented by a node in the graph. We create this node, add
        // edges from the procedural drivers to it, and then add the state node
        // as the new driver for the range.

        auto &stateNode = addNode(std::make_unique<State>(
            &symbol->as<ast::ValueSymbol>(), it.bounds()));

        for (auto &driver : driverList) {
          addEdge(*driver.node, stateNode).setVariable(symbol, it.bounds());
        }

        addDriver(*symbol, nullptr, it.bounds(), &stateNode);

        hookupOutputPort(symbol->as<ast::ValueSymbol>(), it.bounds(),
                         {{&stateNode, nullptr}});
      }

      // TODO: catch hierarchical references for interface hookup.
    }
  }
}

auto NetlistGraph::addPort(const ast::PortSymbol &symbol, DriverBitRange bounds)
    -> NetlistNode & {
  auto &node =
      addNode(std::make_unique<Port>(symbol.direction, symbol.internalSymbol));
  return node;
}

auto NetlistGraph::addModport(ast::ModportPortSymbol const &symbol,
                              DriverBitRange bounds) -> NetlistNode & {
  auto &node = addNode(std::make_unique<Modport>());
  addDriver(symbol, nullptr, bounds, &node);
  return node;
}

NetlistNode *NetlistGraph::lookup(std::string_view name) const {
  auto compare = [&](const std::unique_ptr<NetlistNode> &node) {
    switch (node->kind) {
    case NodeKind::Port:
      return node->as<Port>().internalSymbol->getHierarchicalPath() == name;
    case NodeKind::State:
      return node->as<State>().symbol->getHierarchicalPath() == name;
    default:
      return false;
    }
  };
  auto it = std::ranges::find_if(*this, compare);
  return it != this->end() ? it->get() : nullptr;
}

} // namespace slang::netlist
