#include "NetlistBuilder.hpp"
#include "DataFlowAnalysis.hpp"

#include "netlist/PendingRValue.hpp"
#include "netlist/Utilities.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/HierarchicalReference.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/symbols/InstanceSymbols.h"

#include <BS_thread_pool.hpp>

namespace slang::netlist {

namespace {

/// Thread-local pointer to the deferred work buffer for the current parallel
/// task. nullptr when running sequentially.
thread_local DeferredGraphWork *threadLocalDeferredWork = nullptr;

} // namespace

NetlistBuilder::NetlistBuilder(ast::Compilation &compilation,
                               analysis::AnalysisManager &analysisManager,
                               NetlistGraph &graph)
    : compilation(compilation), analysisManager(analysisManager), graph(graph) {
  NetlistNode::nextID.store(1, std::memory_order_relaxed);
}

auto NetlistBuilder::toTextLocation(SourceLocation loc) const -> TextLocation {
  if (loc.buffer() == SourceLocation::NoLocation.buffer()) {
    return {};
  }
  auto &sm = *compilation.getSourceManager();
  auto fileIdx = graph.fileTable.addFile(sm.getFileName(loc));
  return {fileIdx, sm.getLineNumber(loc), sm.getColumnNumber(loc), loc};
}

auto NetlistBuilder::toSymbolRef(ast::Symbol const &sym) const
    -> SymbolReference {
  return {std::string(sym.name), std::string(sym.getHierarchicalPath()),
          toTextLocation(sym.location)};
}

auto NetlistBuilder::createAssignment(ast::AssignmentExpression const &expr)
    -> NetlistNode & {
  auto node =
      std::make_unique<Assignment>(toTextLocation(expr.sourceRange.start()));
  if (threadLocalDeferredWork) {
    return threadLocalDeferredWork->addNode(std::move(node));
  }
  return graph.addNode(std::move(node));
}

auto NetlistBuilder::createConditional(ast::ConditionalStatement const &stmt)
    -> NetlistNode & {
  auto node =
      std::make_unique<Conditional>(toTextLocation(stmt.sourceRange.start()));
  if (threadLocalDeferredWork) {
    return threadLocalDeferredWork->addNode(std::move(node));
  }
  return graph.addNode(std::move(node));
}

auto NetlistBuilder::createCase(ast::CaseStatement const &stmt)
    -> NetlistNode & {
  auto node = std::make_unique<Case>(toTextLocation(stmt.sourceRange.start()));
  if (threadLocalDeferredWork) {
    return threadLocalDeferredWork->addNode(std::move(node));
  }
  return graph.addNode(std::move(node));
}

void NetlistBuilder::build(const ast::Symbol &root, bool parallel,
                           unsigned numThreads) {
  // Phase 1: Visit the AST sequentially to create ports, variables, and
  // instance structure. Procedural blocks and continuous assignments are
  // deferred.
  collectingPhase = true;
  root.visit(*this);
  collectingPhase = false;

  // Phase 2: Dispatch deferred DFA work items.
  if (parallel) {
    BS::thread_pool pool(numThreads);
    std::mutex exceptionMutex;
    std::exception_ptr pendingException;
    std::vector<DeferredGraphWork> allWork(deferredBlocks.size());

    for (size_t i = 0; i < deferredBlocks.size(); ++i) {
      pool.detach_task([this, &block = deferredBlocks[i], &work = allWork[i],
                        &exceptionMutex, &pendingException] {
        threadLocalDeferredWork = &work;
        SLANG_TRY {
          if (block.isProcedural) {
            handleProceduralBlock(
                block.symbol->as<ast::ProceduralBlockSymbol>());
          } else {
            handleContinuousAssign(
                block.symbol->as<ast::ContinuousAssignSymbol>());
          }
        }
        SLANG_CATCH(const std::exception &) {
          std::lock_guard<std::mutex> lock(exceptionMutex);
          if (!pendingException) {
            pendingException = std::current_exception();
          }
        }
        threadLocalDeferredWork = nullptr;
      });
    }

    pool.wait();

    if (pendingException) {
      std::rethrow_exception(pendingException);
    }

    drainDeferredWork(allWork);
  } else {
    for (auto &block : deferredBlocks) {
      if (block.isProcedural) {
        handleProceduralBlock(block.symbol->as<ast::ProceduralBlockSymbol>());
      } else {
        handleContinuousAssign(block.symbol->as<ast::ContinuousAssignSymbol>());
      }
    }
  }

  deferredBlocks.clear();
}

/// Drain thread-local buffers into the shared graph after all parallel
/// tasks have completed. Must be called single-threaded (after
/// pool.wait()) so that no synchronisation is needed.
void NetlistBuilder::drainDeferredWork(
    std::vector<DeferredGraphWork> &allWork) {
  for (auto &work : allWork) {
    // Move deferred nodes into the shared graph.
    for (auto &node : work.nodes) {
      graph.addNode(std::move(node));
    }
    // Replay deferred edge creation, annotating with symbol/bounds
    // where applicable.
    for (auto &e : work.edges) {
      auto &edge = e.source->addEdge(*e.target);
      if (!e.symbol.empty()) {
        edge.setVariable(std::move(e.symbol), e.bounds);
        edge.setEdgeKind(e.edgeKind);
      }
    }
    // Collect pending R-values for processPendingRvalues() in finalize().
    for (auto &pr : work.pendingRValues) {
      pendingRValues.push_back(std::move(pr));
    }
    // Run deferred mergeDrivers calls that write to the shared driverMap.
    for (auto &fn : work.deferredMerges) {
      fn();
    }
  }
}

void NetlistBuilder::finalize() { processPendingRvalues(); }

void NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target) {
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->edges.push_back(
        {&source, &target, {}, {}, ast::EdgeKind::None});
    return;
  }
  graph.addEdge(source, target);
}

void NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target,
                                   SymbolReference symbol,
                                   DriverBitRange bounds,
                                   ast::EdgeKind edgeKind) {

  // Retrieve the bounds of the driving node, if any.
  auto nodeBounds = source.getBounds();

  // By default, use the specified bounds for the edge.
  auto edgeBounds = bounds;

  // If the source node has specific bounds, intersect them with the specified
  // bounds to determine the actual driven range.
  if (nodeBounds.has_value() && bounds.overlaps(ConstantRange(*nodeBounds))) {
    auto newRange = bounds.intersect(ConstantRange(*nodeBounds));
    edgeBounds = {newRange.lower(), newRange.upper()};
  }

  DEBUG_PRINT("New edge {} from node {} to node {} via {}{}\n",
              toString(edgeKind), source.ID, target.ID, symbol.hierarchicalPath,
              toString(edgeBounds));

  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->edges.push_back(
        {&source, &target, std::move(symbol), edgeBounds, edgeKind});
  } else {
    auto &edge = graph.addEdge(source, target);
    edge.setVariable(std::move(symbol), edgeBounds);
    edge.setEdgeKind(edgeKind);
  }
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

  DEBUG_PRINT("Resolving interface references for symbol {} {} loc={}\n",
              toString(symbol.kind), symbol.name,
              Utilities::locationStr(compilation, symbol.location));

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

        DEBUG_PRINT("Resolved LSP in modport connection expression: {} {} "
                    "bounds={} loc={}\n",
                    toString(symbol.kind), symbol.name, toString(*bounds),
                    Utilities::locationStr(compilation, symbol.location));

        if (symbol.kind == ast::SymbolKind::Variable) {
          // This is an interface variable, so add it to the result.
          result.emplace_back(symbol.as<ast::VariableSymbol>(),
                              DriverBitRange(*bounds));

        } else if (symbol.kind == ast::SymbolKind::ModportPort) {
          // Recurse to follow a nested modport connection.
          _resolveInterfaceRef(alloc, result, evalCtx,
                               symbol.as<ast::ModportPortSymbol>(), lsp);
        } else {
          // The symbol is not an interface variable or modport port — it is
          // likely a parameter or genvar used as an array index in the access
          // expression.  LSPVisitor visits both the array value and the
          // selector, so index symbols reach this callback.  They are not
          // interface signals and should be ignored.
          DEBUG_PRINT("Ignoring non-interface symbol of kind {}\n",
                      toString(symbol.kind));
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
  SLANG_ASSERT(symbol.internalSymbol != nullptr);
  auto ref = toSymbolRef(*symbol.internalSymbol);
  auto &node = graph.addNode(std::make_unique<Port>(
      std::move(ref.name), std::move(ref.hierarchicalPath), ref.location,
      symbol.direction, bounds));
  variables.insert(symbol, bounds, node);
  return node;
}

auto NetlistBuilder::createVariable(ast::VariableSymbol const &symbol,
                                    DriverBitRange bounds) -> NetlistNode & {
  auto ref = toSymbolRef(symbol);
  auto &node = graph.addNode(std::make_unique<Variable>(
      std::move(ref.name), std::move(ref.hierarchicalPath), ref.location,
      bounds));
  variables.insert(symbol, bounds, node);
  return node;
}

auto NetlistBuilder::createState(ast::ValueSymbol const &symbol,
                                 DriverBitRange bounds) -> NetlistNode & {
  auto symRef = toSymbolRef(symbol);
  auto node = std::make_unique<State>(std::move(symRef.name),
                                      std::move(symRef.hierarchicalPath),
                                      symRef.location, bounds);
  auto &ref = threadLocalDeferredWork
                  ? threadLocalDeferredWork->addNode(std::move(node))
                  : graph.addNode(std::move(node));
  variables.insert(symbol, bounds, ref);
  return ref;
}

void NetlistBuilder::addDriversToNode(DriverList const &drivers,
                                      NetlistNode &node, SymbolReference symbol,
                                      DriverBitRange bounds) {
  for (auto driver : drivers) {
    if (driver.node != nullptr) {
      addDependency(*driver.node, node, symbol, bounds);
    }
  }
}

auto NetlistBuilder::merge(NetlistNode &a, NetlistNode &b) -> NetlistNode & {
  if (a.ID == b.ID) {
    return a;
  }

  auto mergeNode = std::make_unique<Merge>();
  auto &node = threadLocalDeferredWork
                   ? threadLocalDeferredWork->addNode(std::move(mergeNode))
                   : graph.addNode(std::move(mergeNode));
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
        addDependency(*varNode, *node, toSymbolRef(symbol), bounds);
      }
    }
    return;
  }

  // Add to the pending list to be processed later.
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->pendingRValues.emplace_back(&symbol, &lsp, bounds,
                                                         node);
  } else {
    pendingRValues.emplace_back(&symbol, &lsp, bounds, node);
  }
}

void NetlistBuilder::processPendingRvalues() {
  for (auto &pending : pendingRValues) {
    DEBUG_PRINT("Processing pending R-value {}{}\n", pending.symbol->name,
                toString(pending.bounds));

    if (pending.node != nullptr) {

      auto symRef = toSymbolRef(*pending.symbol);

      // If there is state variable matching this rvalue.
      if (auto *stateNode = getVariable(*pending.symbol, pending.bounds)) {
        addDependency(*stateNode, *pending.node, symRef, pending.bounds);
        continue;
      }

      // Otherwise, walk the driver intervals that overlap the pending
      // range, emitting an edge per driver annotated with the portion of
      // the driver's range that the pending R-value actually reads. When
      // the interval map has split a single contiguous driver range into
      // abutting sub-intervals, multiple emissions collide on the same
      // (source, target) edge and NetlistEdge::setVariable unions their
      // bounds back into the original range.
      driverMap.forEachDriverInterval(
          drivers, *pending.symbol, pending.bounds,
          [&](DriverBitRange intervalBounds, DriverList const &driverList) {
            auto edgeBounds = intervalBounds.intersection(pending.bounds);
            if (!edgeBounds.has_value()) {
              return;
            }
            for (auto const &source : driverList) {
              addDependency(*source.node, *pending.node, symRef, *edgeBounds);
            }
          });
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

    // Lookup the port node in the graph. The interval map may have split a
    // single contiguous driver range into smaller sub-intervals (because
    // another driver overwrote/merged part of it), so an exact-bounds lookup
    // can miss. Fall back to any port node for this port whose bounds
    // contain the sub-interval.
    const ast::PortSymbol *portSymbol = portBackRef->port;
    NetlistNode *portNode = getVariable(*portSymbol, bounds);
    if (portNode == nullptr) {
      for (auto *candidate : getVariable(*portSymbol)) {
        auto candidateBounds = candidate->getBounds();
        if (candidateBounds.has_value() &&
            ConstantRange(*candidateBounds).contains(bounds)) {
          portNode = candidate;
          break;
        }
      }
    }
    if (portNode != nullptr) {

      // Connect the drivers to the port node(s).
      auto symRef = toSymbolRef(symbol);
      for (auto const &driver : driverList) {
        addDependency(*driver.node, *portNode, symRef, bounds, edgeKind);
      }
    }
  }
}

void NetlistBuilder::mergeDrivers(ast::EvalContext &evalCtx,
                                  ValueTracker const &valueTracker,
                                  ValueDrivers const &valueDrivers,
                                  ast::EdgeKind edgeKind) {
  DEBUG_PRINT("Merging procedural drivers\n");

  valueTracker.visitAll([&](const ast::ValueSymbol *symbol, uint32_t index) {
    DEBUG_PRINT("Symbol {} at index={}\n", symbol->name, index);

    if (index >= valueDrivers.size()) {
      // No drivers for this symbol so we don't need to do anything.
      return;
    }

    if (valueDrivers[index].empty()) {
      // No drivers for this symbol so we don't need to do anything.
      return;
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

        auto symRef = toSymbolRef(*symbol);
        for (auto const &driver : driverList) {
          addDependency(*driver.node, stateNode, symRef, it.bounds(), edgeKind);
        }

        hookupOutputPort(valueSymbol, it.bounds(),
                         {{.node = &stateNode, .lsp = nullptr}}, edgeKind);
      }

      auto symRef = toSymbolRef(*symbol);
      for (auto const &driver : driverList) {

        if (symbol->kind == ast::SymbolKind::ModportPort) {
          // Resolve the interface variables that are driven by a modport port
          // symbol. Add a dependency from the driver to each of the interface
          // variable nodes.
          for (auto &var : resolveInterfaceRef(
                   evalCtx, symbol->as<ast::ModportPortSymbol>(),
                   *driver.lsp)) {
            if (auto *varNode = getVariable(var.symbol, var.bounds)) {
              addDependency(*driver.node, *varNode, symRef, var.bounds);
            }
          }
        } else if (symbol->kind == ast::SymbolKind::Variable) {
          // Check if variable symbols have a node defined for the current
          // bounds. Eg when interface members are assigned to directly.
          if (auto *varNode =
                  getVariable(symbol->as<ast::VariableSymbol>(), it.bounds())) {
            auto varBounds = varNode->getBounds();
            SLANG_ASSERT(varBounds.has_value());
            addDependency(*driver.node, *varNode, symRef, *varBounds);
          }
        }
      }
    }
  });
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

        DEBUG_PRINT("Resolved LSP in port connection expression: {} {} "
                    "bounds={}, loc={}\n",
                    toString(symbol.kind), symbol.name, toString(*bounds),
                    Utilities::locationStr(compilation, symbol.location));

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
  if (collectingPhase) {
    deferredBlocks.push_back({&symbol, /*isProcedural=*/true});
    return;
  }
  handleProceduralBlock(symbol);
}

void NetlistBuilder::handle(ast::ContinuousAssignSymbol const &symbol) {
  if (collectingPhase) {
    deferredBlocks.push_back({&symbol, /*isProcedural=*/false});
    return;
  }
  handleContinuousAssign(symbol);
}

void NetlistBuilder::handleProceduralBlock(
    ast::ProceduralBlockSymbol const &symbol) {
  DEBUG_PRINT("ProceduralBlock\n");
  auto edgeKind = determineEdgeKind(symbol);
  auto dfa = std::make_shared<DataFlowAnalysis>(analysisManager, symbol, *this);
  dfa->run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
  dfa->finalize();
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->deferredMerges.push_back([this, dfa, edgeKind]() {
      mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
                   dfa->getState().valueDrivers, edgeKind);
    });
  } else {
    mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
                 dfa->getState().valueDrivers, edgeKind);
  }
}

void NetlistBuilder::handleContinuousAssign(
    ast::ContinuousAssignSymbol const &symbol) {
  DEBUG_PRINT("ContinuousAssign\n");
  auto dfa = std::make_shared<DataFlowAnalysis>(analysisManager, symbol, *this);
  dfa->run(symbol.getAssignment());
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->deferredMerges.push_back([this, dfa]() {
      mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
                   dfa->getState().valueDrivers, ast::EdgeKind::None);
    });
  } else {
    mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
                 dfa->getState().valueDrivers, ast::EdgeKind::None);
  }
}

void NetlistBuilder::handle(ast::GenerateBlockSymbol const &symbol) {
  if (!symbol.isUninstantiated) {
    visitMembers(symbol);
  }
}

} // namespace slang::netlist
