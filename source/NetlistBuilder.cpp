#include "DataFlowAnalysis.hpp"

#include "netlist/NetlistBuilder.hpp"
#include "netlist/PendingRValue.hpp"
#include "netlist/Utilities.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/HierarchicalReference.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/symbols/InstanceSymbols.h"

namespace slang::netlist {

/// Get the driver bit range for a given node, if it has one.
static auto getNodeBounds(NetlistNode const &node)
    -> std::optional<DriverBitRange> {
  switch (node.kind) {
  case NodeKind::Port:
    return node.as<Port>().bounds;
  case NodeKind::Variable:
    return node.as<Variable>().bounds;
  case NodeKind::State:
    return node.as<State>().bounds;
  default:
    return std::nullopt;
  }
}

NetlistBuilder::NetlistBuilder(ast::Compilation &compilation,
                               analysis::AnalysisManager &analysisManager,
                               NetlistGraph &graph)
    : compilation(compilation), analysisManager(analysisManager), graph(graph) {
  NetlistNode::nextID = 0; // Reset the static ID counter.
}

void NetlistBuilder::finalize() { processPendingRvalues(); }

auto NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target)
    -> NetlistEdge & {
  return graph.addEdge(source, target);
}

void NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target,
                                   ast::Symbol const *symbol,
                                   DriverBitRange bounds,
                                   ast::EdgeKind edgeKind) {

  // Retrieve the bounds of the driving node, if any.
  auto nodeBounds = getNodeBounds(source);

  // By default, use the specified bounds for the edge.
  auto edgeBounds = bounds;

  // If the source node has specific bounds, intersect them with the specified
  // bounds to determine the actual driven range.
  if (nodeBounds.has_value() && bounds.overlaps(ConstantRange(*nodeBounds))) {
    auto newRange = bounds.intersect(ConstantRange(*nodeBounds));
    edgeBounds = {newRange.lower(), newRange.upper()};
  }

  // Add the edge to the graph and annotate the edge with the specified symbol
  // and bounds.
  auto &edge = graph.addEdge(source, target);
  edge.setVariable(symbol, edgeBounds);
  edge.setEdgeKind(edgeKind);

  DEBUG_PRINT("New edge {} from node {} to node {} via {}{}\n",
              toString(edgeKind), source.ID, target.ID,
              symbol->getHierarchicalPath(), toString(edgeBounds));
}

auto NetlistBuilder::getLSPName(ast::ValueSymbol const &symbol,
                                analysis::ValueDriver const &driver)
    -> std::string {
  FormatBuffer buf;
  ast::EvalContext evalContext(symbol);
  ast::LSPUtilities::stringifyLSP(*driver.lsp, evalContext, buf);
  return buf.str();
}

auto NetlistBuilder::determineEdgeKind(ast::ProceduralBlockSymbol const &symbol)
    -> ast::EdgeKind {
  ast::EdgeKind result = ast::EdgeKind::None;

  if (symbol.procedureKind == ast::ProceduralBlockKind::AlwaysFF ||
      symbol.procedureKind == ast::ProceduralBlockKind::Always) {

    if (symbol.getBody().kind == ast::StatementKind::Block) {
      auto const &block = symbol.getBody().as<ast::BlockStatement>();

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

      auto const &events = symbol.getBody()
                               .as<ast::TimedStatement>()
                               .timing.as<ast::EventListControl>()
                               .events;

      // We need to decide if this has the potential for combinational loops
      // The most strict test is if for any unique signal on the event list
      // only one edge (pos or neg) appears e.g. "@(posedge x or negedge x)"
      // is potentially combinational. At the moment we'll settle for no
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

void NetlistBuilder::_resolveInterfaceRef(
    BumpAllocator &alloc, std::vector<InterfaceVarBounds> &result,
    ast::EvalContext &evalCtx, ast::ModportPortSymbol const &symbol,
    ast::Expression const &prefixExpr) {

  auto loc = Utilities::locationStr(compilation, symbol.location);

  DEBUG_PRINT("Resolving interface references for symbol {} {} loc={}\n",
              toString(symbol.kind), symbol.name, loc);

  // Visit all LSPs in the connection expression.
  ast::LSPUtilities::expandIndirectLSPs(
      alloc, prefixExpr, evalCtx,
      [&](const ast::ValueSymbol &symbol, const ast::Expression &lsp,
          bool /*isLValue*/) -> void {
        // Get the bounds of the LSP.
        auto bounds =
            ast::LSPUtilities::getBounds(lsp, evalCtx, symbol.getType());
        if (!bounds) {
          return;
        }

        auto loc = Utilities::locationStr(compilation, symbol.location);

        DEBUG_PRINT("Resolved LSP in modport connection expression: {} {} "
                    "bounds={} loc={}\n",
                    toString(symbol.kind), symbol.name, toString(*bounds), loc);

        if (symbol.kind == ast::SymbolKind::Variable) {
          // This is an interface variable, so add it to the result.
          result.emplace_back(symbol.as<ast::VariableSymbol>(),
                              DriverBitRange(*bounds));

        } else if (symbol.kind == ast::SymbolKind::ModportPort) {
          // Recurse to follow a nested modport connection.
          _resolveInterfaceRef(alloc, result, evalCtx,
                               symbol.as<ast::ModportPortSymbol>(), lsp);
        } else {
          DEBUG_PRINT("Unhandled symbol of kind {}\n", toString(symbol.kind));
          SLANG_UNREACHABLE;
        }
      });
}

auto NetlistBuilder::resolveInterfaceRef(ast::EvalContext &evalCtx,
                                         ast::ModportPortSymbol const &symbol,
                                         ast::Expression const &lsp)
    -> std::vector<InterfaceVarBounds> {

  // This method translates references to modport ports found in
  // in expressions via their connection expressions, to follow modport
  // connections back to the base interface. The underlying interface variable
  // symbol and its access bounds can then be resolved, allowing inputs to be
  // matched with outputs and vice versa.

  BumpAllocator alloc;
  std::vector<InterfaceVarBounds> result;
  _resolveInterfaceRef(alloc, result, evalCtx, symbol, lsp);
  return result;
}

auto NetlistBuilder::createPort(ast::PortSymbol const &symbol,
                                DriverBitRange bounds) -> NetlistNode & {
  auto &node = graph.addNode(std::make_unique<Port>(symbol, bounds));
  variables.insert(symbol, bounds, node);
  return node;
}

auto NetlistBuilder::createVariable(ast::VariableSymbol const &symbol,
                                    DriverBitRange bounds) -> NetlistNode & {
  auto &node = graph.addNode(std::make_unique<Variable>(symbol, bounds));
  variables.insert(symbol, bounds, node);
  return node;
}

auto NetlistBuilder::createState(ast::ValueSymbol const &symbol,
                                 DriverBitRange bounds) -> NetlistNode & {
  auto &node = graph.addNode(std::make_unique<State>(symbol, bounds));
  variables.insert(symbol, bounds, node);
  return node;
}

void NetlistBuilder::addDriversToNode(DriverList const &drivers,
                                      NetlistNode &node,
                                      ast::Symbol const &symbol,
                                      DriverBitRange bounds) {
  for (auto driver : drivers) {
    if (driver.node != nullptr) {
      addDependency(*driver.node, node, &symbol, bounds);
    }
  }
}

auto NetlistBuilder::merge(NetlistNode &a, NetlistNode &b) -> NetlistNode & {
  if (a.ID == b.ID) {
    return a;
  }

  auto &node = graph.addNode(std::make_unique<Merge>());
  addDependency(a, node);
  addDependency(b, node);
  return node;
}

void NetlistBuilder::addRvalue(ast::EvalContext &evalCtx,
                               ast::ValueSymbol const &symbol,
                               ast::Expression const &lsp,
                               DriverBitRange bounds, NetlistNode *node) {

  // For rvalues that are via a modport port, resolve the interface variables
  // they are driven from and add dependencies from each interface variable to
  // the node where the rvalue occurs.
  if (symbol.kind == ast::SymbolKind::ModportPort) {
    for (auto &var : resolveInterfaceRef(
             evalCtx, symbol.as<ast::ModportPortSymbol>(), lsp)) {
      if (auto *varNode = getVariable(var.symbol, var.bounds)) {
        addDependency(*varNode, *node, &symbol, bounds);
      }
    }
    return;
  }

  // Add to the pending list to be processed later.
  pendingRValues.emplace_back(&symbol, &lsp, bounds, node);
}

void NetlistBuilder::processPendingRvalues() {
  for (auto &pending : pendingRValues) {
    DEBUG_PRINT("Processing pending R-value {}{}\n", pending.symbol->name,
                toString(pending.bounds));

    if (pending.node != nullptr) {

      // If there is state variable matching this rvalue.
      if (auto *stateNode = getVariable(*pending.symbol, pending.bounds)) {
        addDependency(*stateNode, *pending.node, pending.symbol.get(),
                      pending.bounds);
        continue;
      }

      // Otherwise, find drivers of the pending R-value, and for each one add
      // edges from the driver to the R-value.
      auto driverList =
          driverMap.getDrivers(drivers, *pending.symbol, pending.bounds);
      for (auto const &source : driverList) {
        addDependency(*source.node, *pending.node, pending.symbol.get(),
                      pending.bounds);
      }
    }
  }
  pendingRValues.clear();
}

void NetlistBuilder::hookupOutputPort(ast::ValueSymbol const &symbol,
                                      DriverBitRange bounds,
                                      DriverList const &driverList,
                                      ast::EdgeKind edgeKind) {

  // If there is an output port associated with this symbol, then add a
  // dependency from the driver to the port.
  if (auto const *portBackRef = symbol.getFirstPortBackref()) {

    if (portBackRef->getNextBackreference() != nullptr) {
      DEBUG_PRINT("Ignoring symbol with multiple port back refs");
      return;
    }

    // Lookup the port node in the graph.
    const ast::PortSymbol *portSymbol = portBackRef->port;
    if (auto *portNode = getVariable(*portSymbol, bounds)) {

      // Connect the drivers to the port node(s).
      for (auto const &driver : driverList) {
        addDependency(*driver.node, *portNode, &symbol, bounds, edgeKind);
      }
    }
  }
}

void NetlistBuilder::mergeDrivers(ast::EvalContext &evalCtx,
                                  ValueTracker const &valueTracker,
                                  ValueDrivers const &valueDrivers,
                                  ast::EdgeKind edgeKind) {
  DEBUG_PRINT("Merging procedural drivers\n");

  for (auto [symbol, index] : valueTracker) {
    DEBUG_PRINT("Symbol {} at index={}\n", symbol->name, index);

    if (index >= valueDrivers.size()) {
      // No drivers for this symbol so we don't need to do anything.
      continue;
    }

    if (valueDrivers[index].empty()) {
      // No drivers for this symbol so we don't need to do anything.
      continue;
    }

    // Merge all of the driver intervals for the symbol into the global map.
    for (auto it = valueDrivers[index].begin(); it != valueDrivers[index].end();
         it++) {

      DEBUG_PRINT("Merging driver interval {}\n", toString(it.bounds()));

      auto const &driverList = valueDrivers[index].getDriverList(*it);
      auto const &valueSymbol = symbol->as<ast::ValueSymbol>();

      if (edgeKind == ast::EdgeKind::None) {

        // Combinational edge, so just add the interval with the driving
        // node(s).
        mergeDrivers(*symbol, it.bounds(), driverList);

        hookupOutputPort(valueSymbol, it.bounds(), driverList);

      } else {

        // Sequential edge, so the procedural drivers act on a stateful
        // variable which is represented by a node in the graph. We create
        // this node, add edges from the procedural drivers to it, and then
        // add the state node as the new driver for the range.

        auto &stateNode = createState(valueSymbol, it.bounds());

        for (auto const &driver : driverList) {
          addDependency(*driver.node, stateNode, symbol, it.bounds(), edgeKind);
        }

        hookupOutputPort(valueSymbol, it.bounds(),
                         {{.node = &stateNode, .lsp = nullptr}}, edgeKind);
      }

      for (auto const &driver : driverList) {

        if (symbol->kind == ast::SymbolKind::ModportPort) {
          // Resolve the interface variables that are driven by a modport port
          // symbol. Add a dependency from the driver to each of the interface
          // variable nodes.
          for (auto &var : resolveInterfaceRef(
                   evalCtx, symbol->as<ast::ModportPortSymbol>(),
                   *driver.lsp)) {
            if (auto *varNode = getVariable(var.symbol, var.bounds)) {
              addDependency(*driver.node, *varNode, symbol, var.bounds);
            }
          }
        } else if (symbol->kind == ast::SymbolKind::Variable) {
          // Check if variable symbols have a node defined for the current
          // bounds. Eg when interface members are assigned to directly.
          if (auto *varNode =
                  getVariable(symbol->as<ast::VariableSymbol>(), it.bounds())) {
            auto varBounds = getNodeBounds(*varNode);
            SLANG_ASSERT(varBounds.has_value());
            addDependency(*driver.node, *varNode, symbol, *varBounds);
          }
        }
      }
    }
  }
}

void NetlistBuilder::handlePortConnection(
    ast::Symbol const &containingSymbol,
    ast::PortConnection const &portConnection) {

  auto const &port = portConnection.port.as<ast::PortSymbol>();
  auto const *expr = portConnection.getExpression();

  if (expr == nullptr || expr->bad()) {
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

  auto portNodes = getVariable(port);
  DEBUG_PRINT("Port {} has {} nodes\n", port.name, portNodes.size());

  // Visit all LSPs in the connection expression.
  ast::LSPUtilities::visitLSPs(
      *expr, evalCtx,
      [&](const ast::ValueSymbol &symbol, const ast::Expression &lsp,
          bool /*isLValue*/) -> void {
        // Get the bounds of the LSP.
        auto bounds =
            ast::LSPUtilities::getBounds(lsp, evalCtx, symbol.getType());
        if (!bounds) {
          return;
        }

        auto loc = Utilities::locationStr(compilation, symbol.location);
        DEBUG_PRINT("Resolved LSP in port connection expression: {} {} "
                    "bounds={}, loc={}\n",
                    toString(symbol.kind), symbol.name, toString(*bounds), loc);

        for (auto *node : portNodes) {
          auto driverBounds = DriverBitRange(*bounds);
          if (isOutput) {
            // If lvalue, then the port defines symbol with bounds.
            // FIXME: *Merge* the driver there is currently no way to tell what
            // bounds the lsp occupies within the port type and to drive
            // appropriately.
            mergeDrivers(symbol, driverBounds, {DriverInfo(node, &lsp)});
            hookupOutputPort(symbol, driverBounds, {DriverInfo(node, nullptr)});
          } else {
            // If rvalue, then the port is driven by symbol with bounds.
            addRvalue(evalCtx, symbol, lsp, driverBounds, node);
          }
        }
      });
}

void NetlistBuilder::handle(ast::PortSymbol const &symbol) {
  DEBUG_PRINT("PortSymbol {}\n", symbol.name);

  if (symbol.internalSymbol != nullptr && symbol.internalSymbol->isValue()) {
    auto const &valueSymbol = symbol.internalSymbol->as<ast::ValueSymbol>();
    auto drivers = analysisManager.getDrivers(valueSymbol);
    for (auto &[driver, bounds] : drivers) {

      DEBUG_PRINT("{} driven by prefix={}\n", toString(bounds),
                  getLSPName(valueSymbol, *driver));

      // Add a port node for the driven range, and add a driver entry for it.
      // Note that the driver key is a PortSymbol, rather than a ValueSymbol.
      auto &node = createPort(symbol, DriverBitRange(bounds));

      // If the driver is an input port, then create a dependency to the
      // internal symbol (ValueSymbol).
      if (driver->isInputPort()) {
        addDriver(valueSymbol, nullptr, DriverBitRange(bounds), &node);
      }
    }
  }
}

void NetlistBuilder::handle(ast::VariableSymbol const &symbol) {

  // Identify interface variables.
  if (auto const *scope = symbol.getParentScope()) {
    auto const *container = scope->getContainingInstance();
    if (container != nullptr && container->parentInstance != nullptr) {
      if (container->parentInstance->isInterface()) {
        DEBUG_PRINT("Interface variable {}\n", symbol.name);

        auto drivers = analysisManager.getDrivers(symbol);
        for (auto &[driver, bounds] : drivers) {

          DEBUG_PRINT("[{}:{}] driven by prefix={}\n", bounds.first,
                      bounds.second, getLSPName(symbol, *driver));

          // Create a variable node for the interface member's driven range.
          createVariable(symbol, DriverBitRange(bounds));
        }
      }
    }
  }
}

void NetlistBuilder::handle(ast::InstanceSymbol const &symbol) {
  DEBUG_PRINT("InstanceSymbol {}\n", symbol.name);

  if (symbol.body.flags.has(ast::InstanceFlags::Uninstantiated)) {
    return;
  }

  symbol.body.visit(*this);

  // Handle port connections.
  for (auto const *portConnection : symbol.getPortConnections()) {

    if (portConnection->port.kind == ast::SymbolKind::Port) {
      handlePortConnection(symbol, *portConnection);
    } else if (portConnection->port.kind == ast::SymbolKind::InterfacePort) {
      // Interfaces are handled via ModportPorts.
    } else {
      SLANG_UNREACHABLE;
    }
  }
}

void NetlistBuilder::handle(ast::ProceduralBlockSymbol const &symbol) {
  DEBUG_PRINT("ProceduralBlock\n");
  auto edgeKind = determineEdgeKind(symbol);
  DataFlowAnalysis dfa(analysisManager, symbol, *this);
  dfa.run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
  dfa.finalize();
  mergeDrivers(dfa.getEvalContext(), dfa.valueTracker,
               dfa.getState().valueDrivers, edgeKind);
}

void NetlistBuilder::handle(ast::ContinuousAssignSymbol const &symbol) {
  DEBUG_PRINT("ContinuousAssign\n");
  DataFlowAnalysis dfa(analysisManager, symbol, *this);
  dfa.run(symbol.getAssignment());
  mergeDrivers(dfa.getEvalContext(), dfa.valueTracker,
               dfa.getState().valueDrivers, ast::EdgeKind::None);
}

void NetlistBuilder::handle(ast::GenerateBlockSymbol const &symbol) {
  if (!symbol.isUninstantiated) {
    visitMembers(symbol);
  }
}

} // namespace slang::netlist
