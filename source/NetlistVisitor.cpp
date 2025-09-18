#include "netlist/NetlistVisitor.hpp"

namespace slang::netlist {

std::string NetlistVisitor::getLSPName(const ast::ValueSymbol &symbol,
                                       const analysis::ValueDriver &driver) {
  FormatBuffer buf;
  ast::EvalContext evalContext(symbol);
  ast::LSPUtilities::stringifyLSP(*driver.prefixExpression, evalContext, buf);
  return buf.str();
}

ast::EdgeKind
NetlistVisitor::determineEdgeKind(const ast::ProceduralBlockSymbol &symbol) {
  ast::EdgeKind result = ast::EdgeKind::None;

  if (symbol.procedureKind == ast::ProceduralBlockKind::AlwaysFF ||
      symbol.procedureKind == ast::ProceduralBlockKind::Always) {

    if (symbol.getBody().kind == ast::StatementKind::Block) {
      auto &block = symbol.getBody().as<ast::BlockStatement>();

      if (block.blockKind == ast::StatementBlockKind::Sequential &&
          block.body.kind == ast::StatementKind::ConcurrentAssertion) {
        return result;
      }
    }

    auto tck = symbol.getBody().as<ast::TimedStatement>().timing.kind;

    if (tck == ast::TimingControlKind::SignalEvent) {
      result = symbol.getBody()
                   .as<ast::TimedStatement>()
                   .timing.as<ast::SignalEventControl>()
                   .edge;

    } else if (tck == ast::TimingControlKind::EventList) {

      auto &events = symbol.getBody()
                         .as<ast::TimedStatement>()
                         .timing.as<ast::EventListControl>()
                         .events;

      // We need to decide if this has the potential for combinatorial loops
      // The most strict test is if for any unique signal on the event list
      // only one edge (pos or neg) appears e.g. "@(posedge x or negedge x)"
      // is potentially combinatorial. At the moment we'll settle for no
      // signal having "None" edge.

      for (auto const &e : events) {
        result = e->as<ast::SignalEventControl>().edge;
        if (result == ast::EdgeKind::None) {
          break;
        }
      }

      // If we got here, edgeKind is not "None" which is all we care about.
    }
  }

  return result;
}

NetlistVisitor::NetlistVisitor(ast::Compilation &compilation,
                               analysis::AnalysisManager &analysisManager,
                               NetlistGraph &graph)
    : compilation(compilation), analysisManager(analysisManager), graph(graph) {
}

void NetlistVisitor::handle(const ast::PortSymbol &symbol) {
  DEBUG_PRINT("PortSymbol {}\n", symbol.name);

  if (symbol.internalSymbol && symbol.internalSymbol->isValue()) {
    auto const &valueSymbol = symbol.internalSymbol->as<ast::ValueSymbol>();
    auto drivers = analysisManager.getDrivers(valueSymbol);
    for (auto &[driver, bounds] : drivers) {

      DEBUG_PRINT("[{}:{}] driven by prefix={}\n", bounds.first, bounds.second,
                  getLSPName(valueSymbol, *driver));

      // Add a port node for the driven range.
      auto &node = graph.addPort(symbol, bounds);

      // If the driver is an input port, then create a dependency to the
      // internal symbol.
      if (driver->isInputPort()) {
        graph.addDriver(valueSymbol, bounds, &node);
      }
    }
  }
}

void NetlistVisitor::handle(const ast::InterfacePortSymbol &symbol) {
  DEBUG_PRINT("InterfacePortSymbol\n");
}

void NetlistVisitor::handle(const ast::InstanceSymbol &symbol) {
  DEBUG_PRINT("InstanceSymbol {}\n", symbol.name);

  if (symbol.body.flags.has(ast::InstanceFlags::Uninstantiated)) {
    DEBUG_PRINT("(Uninstantiated)\n");
    return;
  }

  symbol.body.visit(*this);

  // Handle port connections.
  for (auto portConnection : symbol.getPortConnections()) {

    if (portConnection->port.kind == ast::SymbolKind::Port) {

      auto &port = portConnection->port.as<ast::PortSymbol>();
      auto direction = portConnection->port.as<ast::PortSymbol>().direction;
      auto *internalSymbol = port.internalSymbol;

      if (portConnection->getExpression() == nullptr) {
        // Empty port hookup so skip.
        continue;
      }

      if (internalSymbol && internalSymbol->isValue()) {

        // Ports are connected to an internal ValueSymbol.
        auto &valueSymbol = internalSymbol->as<ast::ValueSymbol>();

        // Access the PortSymbol via the connected internal symbol's back
        // reference.
        const ast::PortSymbol *portSymbol{nullptr};
        if (auto *portBackRef = valueSymbol.getFirstPortBackref()) {

          if (portBackRef->getNextBackreference()) {
            DEBUG_PRINT("Ignoring symbol with multiple port back refs");
            return;
          }

          portSymbol = portBackRef->port;
        }

        if (portSymbol == nullptr) {
          // Unconnected port so skip.
          continue;
        }

        // Lookup the port node(s) in the graph by the PortSymbol and bounds of
        // the PortSymbol's declared type. It is expected that a PortSymbol maps
        // to a single NetlistNode, but the following code can handle multiple
        // nodes.
        auto &type = valueSymbol.getType();
        if (type.hasFixedRange()) {
          auto range = type.getFixedRange();
          DEBUG_PRINT("Internal port symbol range [{}:{}]\n", range.lower(),
                      range.upper());

          for (auto *driverNode :
               graph.getDrivers(*portSymbol, {range.lower(), range.upper()})) {
            DEBUG_PRINT("Node for port\n");

            // Run the DFA to hookup values to or from the port node depending
            // on its direction.
            DataFlowAnalysis dfa(analysisManager, symbol, graph, driverNode);
            dfa.run(*portConnection->getExpression());
            graph.mergeDrivers(dfa.symbolToSlot, dfa.getState().definitions);

            // Special handling for output ports to create a dependency
            // between the port netlist node and the assignment of the port
            // to the connection expression. The DFA produces an assignment
            // node, so connect to that via the final DFA state.
            if (direction == ast::ArgumentDirection::Out) {
              SLANG_ASSERT(dfa.getState().node);
              auto &edge = graph.addEdge(*driverNode, *dfa.getState().node);
            }
          }
        }
      }

    } else if (portConnection->port.kind == ast::SymbolKind::InterfacePort) {
      DEBUG_PRINT("Unhandled interface port connection\n");

    } else {
      SLANG_UNREACHABLE;
    }
  }
}

void NetlistVisitor::handle(const ast::ProceduralBlockSymbol &symbol) {
  DEBUG_PRINT("ProceduralBlock\n");
  auto edgeKind = determineEdgeKind(symbol);
  DataFlowAnalysis dfa(analysisManager, symbol, graph);
  dfa.run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
  dfa.finalize();
  graph.mergeDrivers(dfa.symbolToSlot, dfa.getState().definitions, edgeKind);
}

void NetlistVisitor::handle(const ast::ContinuousAssignSymbol &symbol) {
  DEBUG_PRINT("ContinuousAssign\n");
  DataFlowAnalysis dfa(analysisManager, symbol, graph);
  dfa.run(symbol.getAssignment());
  graph.mergeDrivers(dfa.symbolToSlot, dfa.getState().definitions,
                     ast::EdgeKind::None);
}

void NetlistVisitor::handle(const ast::GenerateBlockSymbol &symbol) {
  if (!symbol.isUninstantiated) {
    visitMembers(symbol);
  }
}

} // namespace slang::netlist
