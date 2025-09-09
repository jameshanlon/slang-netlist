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
  graph.addPort(symbol);
}

void NetlistVisitor::handle(const ast::ValueSymbol &symbol) {
  DEBUG_PRINT("ValueSymbol {}\n", symbol.name);
  auto drivers = analysisManager.getDrivers(symbol);

  for (auto &[driver, bounds] : drivers) {
    DEBUG_PRINT("  Driven by {} [{}:{}] prefix={}\n", toString(driver->kind),
                bounds.first, bounds.second, getLSPName(symbol, *driver));

    if (driver->isInputPort()) {
      DEBUG_PRINT("  driven by input port\n");
      graph.connectInputPort(symbol, bounds);

    } else if (driver->flags.has(analysis::DriverFlags::OutputPort)) {
      DEBUG_PRINT("  driving output port\n");
      // Connection of output ports is handled in the port connection
      // code of the instance symbol visitor below.
    }
  }
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

      if (direction == ast::ArgumentDirection::In) {
        DEBUG_PRINT("Input port int sym={}\n", internalSymbol->name);

      } else if (direction == ast::ArgumentDirection::Out) {
        DEBUG_PRINT("Output port int sym={}\n", internalSymbol->name);

      } else {
        DEBUG_PRINT("Unhandled port connection type\n");
      }

      if (portConnection->getExpression() == nullptr) {
        // Empty port hookup so skip.
        continue;
      }

      // Lookup the port node in the graph by the internal symbol.
      // Run the DFA to hookup values to or from the port node
      // depending on its direction.
      auto node = graph.getPort(port.internalSymbol);

      DataFlowAnalysis dfa(analysisManager, symbol, graph, *node);
      dfa.run(*portConnection->getExpression());
      graph.mergeDrivers(dfa.symbolToSlot, dfa.getState().definitions);

      // Special handling for output ports to create a dependency
      // between the port netlist node and the assignment of the port
      // to the connection expression. The DFA produces an assignment
      // node, so connect to that via the final DFA state.

      if (direction == ast::ArgumentDirection::Out) {

        SLANG_ASSERT(dfa.getState().node);
        auto &edge = graph.addEdge(**graph.getPort(port.internalSymbol),
                                   *dfa.getState().node);
      }

    } else if (portConnection->port.kind == ast::SymbolKind::InterfacePort) {
      DEBUG_PRINT("Unhandled interface port connection\n");

    } else if (portConnection->port.kind == ast::SymbolKind::MultiPort) {
      DEBUG_PRINT("Unhandled multi port connection\n");

    } else if (portConnection->port.kind == ast::SymbolKind::ModportPort) {
      DEBUG_PRINT("Unhandled modport port connection\n");

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
