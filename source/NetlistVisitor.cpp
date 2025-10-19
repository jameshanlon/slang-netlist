#include "netlist/NetlistVisitor.hpp"
#include "netlist/ReportingUtilities.hpp"

namespace slang::netlist {

std::string NetlistVisitor::getLSPName(ast::ValueSymbol const &symbol,
                                       analysis::ValueDriver const &driver) {
  FormatBuffer buf;
  ast::EvalContext evalContext(symbol);
  ast::LSPUtilities::stringifyLSP(*driver.prefixExpression, evalContext, buf);
  return buf.str();
}

ast::EdgeKind
NetlistVisitor::determineEdgeKind(ast::ProceduralBlockSymbol const &symbol) {
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
                               NetlistBuilder &graph)
    : compilation(compilation), analysisManager(analysisManager),
      builder(graph) {}

void NetlistVisitor::handle(const ast::PortSymbol &symbol) {
  DEBUG_PRINT("PortSymbol {}\n", symbol.name);

  if (symbol.internalSymbol && symbol.internalSymbol->isValue()) {
    auto const &valueSymbol = symbol.internalSymbol->as<ast::ValueSymbol>();
    auto drivers = analysisManager.getDrivers(valueSymbol);
    for (auto &[driver, bounds] : drivers) {

      DEBUG_PRINT("[{}:{}] driven by prefix={}\n", bounds.first, bounds.second,
                  getLSPName(valueSymbol, *driver));

      // Add a port node for the driven range, and add a driver entry for it.
      // Note that the driver key is a PortSymbol, rather than a ValueSymbol.
      auto &node = builder.createPort(symbol, bounds);

      // If the driver is an input port, then create a dependency to the
      // internal symbol (ValueSymbol).
      if (driver->isInputPort()) {
        builder.addDriver(valueSymbol, nullptr, bounds, &node);
      }
    }
  }
}

void NetlistVisitor::handle(const ast::VariableSymbol &symbol) {

  // Identify interface variables.
  if (auto scope = symbol.getParentScope()) {
    auto container = scope->getContainingInstance();
    if (container && container->parentInstance) {
      if (container->parentInstance->isInterface()) {
        DEBUG_PRINT("Interface variable {}\n", symbol.name);

        auto drivers = analysisManager.getDrivers(symbol);
        for (auto &[driver, bounds] : drivers) {

          DEBUG_PRINT("[{}:{}] driven by prefix={}\n", bounds.first,
                      bounds.second, getLSPName(symbol, *driver));

          // Create a variable node for the interface member's driven range.
          builder.createVariable(symbol, bounds);
        }
      }
    }
  }
}

void NetlistVisitor::handlePortConnection(
    ast::Symbol const &containingSymbol,
    ast::PortConnection const &portConnection) {

  auto &port = portConnection.port.as<ast::PortSymbol>();
  auto direction = portConnection.port.as<ast::PortSymbol>().direction;
  auto *internalSymbol = port.internalSymbol;
  auto *expr = portConnection.getExpression();

  if (!expr || expr->bad()) {
    // Empty port hookup so skip.
    return;
  }

  ast::EvalContext evalCtx(containingSymbol);

  // Remove the assignment from output port connection expressions.
  bool isOutput{false};
  if (expr->kind == ast::ExpressionKind::Assignment) {
    expr = &expr->as<ast::AssignmentExpression>().left();
    isOutput = true;
  }

  auto portNodes = builder.getVariable(port);
  DEBUG_PRINT("Port {} has {} nodes\n", port.name, portNodes.size());

  // Visit all LSPs in the connection expression.
  ast::LSPUtilities::visitLSPs(
      *expr, evalCtx,
      [&](const ast::ValueSymbol &symbol, const ast::Expression &lsp,
          bool isLValue) -> void {
        // Get the bounds of the LSP.
        auto bounds =
            ast::LSPUtilities::getBounds(lsp, evalCtx, symbol.getType());
        if (!bounds) {
          return;
        }

        auto loc =
            ReportingUtilities::locationStr(compilation, symbol.location);
        DEBUG_PRINT("Resolved LSP in port connection expression: {} {} "
                    "bounds=[{}:{}] loc={}\n",
                    toString(symbol.kind), symbol.name, bounds->first,
                    bounds->second, loc);

        for (auto *node : portNodes) {
          if (isOutput) {
            // If lvalue, then the port defines symbol with bounds.
            // FIXME: *Merge* the driver there is currently no way to tell what
            // bounds the lsp occupies within the port type and to drive
            // appropriately.
            builder.mergeDriver(symbol, &lsp, *bounds, node);
            builder.hookupOutputPort(symbol, *bounds,
                                     {DriverInfo(node, nullptr)});
          } else {
            // If rvalue, then the port is driven by symbol with bounds.
            builder.addRvalue(evalCtx, symbol, lsp, *bounds, node);
          }
        }
      });
}

void NetlistVisitor::handle(ast::InstanceSymbol const &symbol) {
  DEBUG_PRINT("InstanceSymbol {}\n", symbol.name);

  if (symbol.body.flags.has(ast::InstanceFlags::Uninstantiated)) {
    return;
  }

  symbol.body.visit(*this);

  // Handle port connections.
  for (auto portConnection : symbol.getPortConnections()) {

    if (portConnection->port.kind == ast::SymbolKind::Port) {
      handlePortConnection(symbol, *portConnection);
    } else if (portConnection->port.kind == ast::SymbolKind::InterfacePort) {
      // Interfaces are handled via ModportPorts.
    } else {
      SLANG_UNREACHABLE;
    }
  }
}

void NetlistVisitor::handle(ast::ProceduralBlockSymbol const &symbol) {
  DEBUG_PRINT("ProceduralBlock\n");
  auto edgeKind = determineEdgeKind(symbol);
  DataFlowAnalysis dfa(analysisManager, symbol, builder);
  dfa.run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
  dfa.finalize();
  builder.mergeProcDrivers(dfa.getEvalContext(), dfa.symbolTracker,
                           dfa.getState().symbolDrivers, edgeKind);
}

void NetlistVisitor::handle(ast::ContinuousAssignSymbol const &symbol) {
  DEBUG_PRINT("ContinuousAssign\n");
  DataFlowAnalysis dfa(analysisManager, symbol, builder);
  dfa.run(symbol.getAssignment());
  builder.mergeProcDrivers(dfa.getEvalContext(), dfa.symbolTracker,
                           dfa.getState().symbolDrivers, ast::EdgeKind::None);
}

void NetlistVisitor::handle(ast::GenerateBlockSymbol const &symbol) {
  if (!symbol.isUninstantiated) {
    visitMembers(symbol);
  }
}

} // namespace slang::netlist
