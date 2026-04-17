#include "NetlistBuilder.hpp"
#include "DataFlowAnalysis.hpp"
#include "PendingRValue.hpp"

#include "netlist/Utilities.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/HierarchicalReference.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/symbols/InstanceSymbols.h"

#include "slang/util/FlatMap.h"

namespace slang::netlist {

namespace {

/// Thread-local pointer to the deferred work buffer for the current parallel
/// task. nullptr when running sequentially.
thread_local DeferredGraphWork *threadLocalDeferredWork = nullptr;

/// Thread-local cache mapping AST symbols to their materialized
/// SymbolReference. Populated lazily by toSymbolRef() to avoid repeated
/// hierarchicalPath string construction and FileTable accesses.  It is cleared
/// at the start of each parallel task and at the start of each sequential
/// build() so stale entries never leak.
thread_local flat_hash_map<const ast::Symbol *, SymbolReference>
    threadLocalSymbolRefCache;

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
  auto it = threadLocalSymbolRefCache.find(&sym);
  if (it != threadLocalSymbolRefCache.end()) {
    return it->second;
  }
  SymbolReference ref{std::string(sym.name),
                      std::string(sym.getHierarchicalPath()),
                      toTextLocation(sym.location)};
  threadLocalSymbolRefCache.emplace(&sym, ref);
  return ref;
}

auto NetlistBuilder::createAssignment(ast::AssignmentExpression const &expr)
    -> NetlistNode & {
  auto node =
      std::make_unique<Assignment>(toTextLocation(expr.sourceRange.start()));
  return graph.addNode(std::move(node));
}

auto NetlistBuilder::createConditional(ast::ConditionalStatement const &stmt)
    -> NetlistNode & {
  auto node =
      std::make_unique<Conditional>(toTextLocation(stmt.sourceRange.start()));
  return graph.addNode(std::move(node));
}

auto NetlistBuilder::createCase(ast::CaseStatement const &stmt)
    -> NetlistNode & {
  auto node = std::make_unique<Case>(toTextLocation(stmt.sourceRange.start()));
  return graph.addNode(std::move(node));
}

void NetlistBuilder::build(const ast::Symbol &root, bool parallel,
                           unsigned numThreads) {
  using Clock = std::chrono::steady_clock;
  parallel_ = parallel;

  // Phase 1: Visit the AST sequentially to create ports, variables, and
  // instance structure. Procedural blocks and continuous assignments are
  // deferred.

  // Clear the main-thread symbol-ref cache so entries from a prior build()
  // (whose Compilation may have been destroyed and whose Symbol addresses
  // may now be reused) cannot produce stale hits.
  threadLocalSymbolRefCache.clear();

  auto t0 = Clock::now();
  collectingPhase = true;
  root.visit(*this);
  collectingPhase = false;
  auto t1 = Clock::now();

  profile.phase1_collectSeconds =
      std::chrono::duration<double>(t1 - t0).count();
  profile.deferredBlockCount = deferredBlocks.size();
  profile.numThreads = numThreads;

  // Phase 2: Dispatch deferred DFA work items.
  auto t2 = Clock::now();
  if (parallel) {
    threadPool = std::make_unique<BS::thread_pool<>>(numThreads);
    std::mutex exceptionMutex;
    std::exception_ptr pendingException;
    std::vector<DeferredGraphWork> allWork(deferredBlocks.size());

    for (size_t i = 0; i < deferredBlocks.size(); ++i) {
      threadPool->detach_task([this, &block = deferredBlocks[i], &work = allWork[i],
                        &exceptionMutex, &pendingException] {
        auto taskStart = Clock::now();
        threadLocalDeferredWork = &work;
        threadLocalSymbolRefCache.clear();
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
        work.elapsedSeconds =
            std::chrono::duration<double>(Clock::now() - taskStart).count();
      });
    }

    threadPool->wait();
    auto t3 = Clock::now();
    profile.phase2_parallelSeconds =
        std::chrono::duration<double>(t3 - t2).count();

    if (pendingException) {
      std::rethrow_exception(pendingException);
    }

    // Compute per-task statistics.
    if (!allWork.empty()) {
      std::vector<double> taskTimes;
      taskTimes.reserve(allWork.size());
      for (auto &work : allWork) {
        taskTimes.push_back(work.elapsedSeconds);
      }
      std::sort(taskTimes.begin(), taskTimes.end());
      profile.taskMinSeconds = taskTimes.front();
      profile.taskMaxSeconds = taskTimes.back();
      profile.taskTotalSeconds =
          std::accumulate(taskTimes.begin(), taskTimes.end(), 0.0);
      profile.taskMeanSeconds =
          profile.taskTotalSeconds / static_cast<double>(taskTimes.size());
      auto mid = taskTimes.size() / 2;
      profile.taskMedianSeconds =
          (taskTimes.size() % 2 == 0)
              ? (taskTimes[mid - 1] + taskTimes[mid]) / 2.0
              : taskTimes[mid];
    }

    auto t4 = Clock::now();
    drainDeferredWork(allWork);
    profile.phase3_drainSeconds =
        std::chrono::duration<double>(Clock::now() - t4).count();
  } else {
    threadLocalSymbolRefCache.clear();
    for (auto &block : deferredBlocks) {
      if (block.isProcedural) {
        handleProceduralBlock(block.symbol->as<ast::ProceduralBlockSymbol>());
      } else {
        handleContinuousAssign(block.symbol->as<ast::ContinuousAssignSymbol>());
      }
    }
    profile.phase2_parallelSeconds =
        std::chrono::duration<double>(Clock::now() - t2).count();
  }

  deferredBlocks.clear();
}

/// Collect pending R-values from thread-local buffers after all parallel
/// Phase 2 tasks have completed, for Phase 4 resolution.
void NetlistBuilder::drainDeferredWork(
    std::vector<DeferredGraphWork> &allWork) {
  for (auto &work : allWork) {
    profile.deferredPendingRValueCount += work.pendingRValues.size();
    for (auto &pr : work.pendingRValues) {
      pendingRValues.push_back(std::move(pr));
    }
  }
  profile.drain_pendingRValuesSeconds = 0;
  profile.drain_mergesSeconds = 0;
}

void NetlistBuilder::finalize() {
  using Clock = std::chrono::steady_clock;
  auto t0 = Clock::now();
  processPendingRvalues();
  threadPool.reset();
  profile.phase4_rvalueSeconds =
      std::chrono::duration<double>(Clock::now() - t0).count();
}

void NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target) {
  source.addEdge(target);
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
    // Parallel Phase 2: always create a new edge to avoid the
    // check-then-act race on setVariable with addEdge.
    auto &edge = source.addNewEdge(target);
    edge.setVariable(std::move(symbol), edgeBounds);
    edge.setEdgeKind(edgeKind);
  } else {
    // During sequential phases, use addEdge (deduplicated) to merge
    // bounds of abutting driver intervals into a single edge annotation.
    auto &edge = source.addEdge(target);
    if (!edge.setVariable(std::move(symbol), edgeBounds)) {
      // Existing edge carries a non-contiguous range for the same symbol;
      // create a parallel edge to preserve exact bit-range accuracy.
      auto &newEdge = source.addNewEdge(target);
      newEdge.setVariable(std::move(symbol), edgeBounds);
      newEdge.setEdgeKind(edgeKind);
    } else {
      edge.setEdgeKind(edgeKind);
    }
  }
}

auto NetlistBuilder::getDriverPathName(ast::ValueSymbol const &symbol,
                                       analysis::ValueDriver const &driver)
    -> std::string {
  ast::EvalContext evalContext(symbol);
  return driver.path.toString(evalContext);
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

    if (symbol.getBody().kind != ast::StatementKind::Timed) {
      return result;
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
  ast::ValuePath prefixPath(prefixExpr, evalCtx);
  prefixPath.expandIndirectRefs(
      alloc, evalCtx, [&](const ast::ValuePath &path) -> void {
        if (path.empty() || !path.lsp) {
          return;
        }
        auto const *rootSymbol = path.rootSymbol();
        if (rootSymbol == nullptr) {
          return;
        }
        auto const &symbol = *rootSymbol;
        auto bounds = path.lspBounds;
        auto const &lsp = *path.lsp;

        DEBUG_PRINT("Resolved LSP in modport connection expression: "
                    "{} {} bounds={} loc={}\n",
                    toString(symbol.kind), symbol.name, toString(bounds),
                    Utilities::locationStr(compilation, symbol.location));

        if (symbol.kind == ast::SymbolKind::Variable) {
          // This is an interface variable, so add it to the result.
          result.emplace_back(symbol.as<ast::VariableSymbol>(),
                              DriverBitRange(bounds));

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
  auto &ref = graph.addNode(std::move(node));
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
  auto &node = graph.addNode(std::move(mergeNode));
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
  if (symbol.kind == ast::SymbolKind::ModportPort && node != nullptr) {
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
  if (!parallel_ || !threadPool || pendingRValues.size() < 1000) {
    // Sequential path: original logic.
    for (auto &pending : pendingRValues) {
      if (pending.node == nullptr) {
        continue;
      }
      DEBUG_PRINT("Processing pending R-value {}{}\n", pending.symbol->name,
                  toString(pending.bounds));

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
              if (source.node != nullptr) {
                addDependency(*source.node, *pending.node, symRef,
                              *edgeBounds);
              }
            }
          });
    }
    pendingRValues.clear();
    return;
  }

  // Parallel path: partition by target node.
  std::unordered_map<NetlistNode *, std::vector<size_t>> partitions;
  for (size_t i = 0; i < pendingRValues.size(); ++i) {
    if (pendingRValues[i].node != nullptr) {
      partitions[pendingRValues[i].node].push_back(i);
    }
  }

  // Flatten partition keys for chunked dispatch.
  std::vector<NetlistNode *> targets;
  targets.reserve(partitions.size());
  for (auto &[node, _] : partitions) {
    targets.push_back(node);
  }

  std::mutex exceptionMutex;
  std::exception_ptr pendingException;

  // Dispatch chunks of target nodes to threads.
  threadPool->detach_blocks(
      static_cast<size_t>(0), targets.size(),
      [&](size_t begin, size_t end) {
        threadLocalSymbolRefCache.clear();
        for (size_t t = begin; t < end; ++t) {
          auto *targetNode = targets[t];
          for (size_t idx : partitions[targetNode]) {
            auto &pending = pendingRValues[idx];
            SLANG_TRY {
              auto symRef = toSymbolRef(*pending.symbol);

              if (auto *stateNode =
                      getVariable(*pending.symbol, pending.bounds)) {
                stateNode->addNewEdge(*pending.node)
                    .setVariable(std::move(symRef), pending.bounds);
                continue;
              }

              driverMap.forEachDriverInterval(
                  drivers, *pending.symbol, pending.bounds,
                  [&](DriverBitRange intervalBounds,
                      DriverList const &driverList) {
                    auto edgeBounds =
                        intervalBounds.intersection(pending.bounds);
                    if (!edgeBounds.has_value()) {
                      return;
                    }
                    for (auto const &source : driverList) {
                      if (source.node != nullptr) {
                        auto &edge =
                            source.node->addNewEdge(*pending.node);
                        edge.setVariable(symRef, *edgeBounds);
                      }
                    }
                  });
            }
            SLANG_CATCH(const std::exception &) {
              std::lock_guard<std::mutex> lock(exceptionMutex);
              if (!pendingException) {
                pendingException = std::current_exception();
              }
            }
          }
        }
      });

  threadPool->wait();

  if (pendingException) {
    std::rethrow_exception(pendingException);
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
        if (driver.node != nullptr) {
          addDependency(*driver.node, *portNode, symRef, bounds, edgeKind);
        }
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
          if (driver.node != nullptr) {
            addDependency(*driver.node, stateNode, symRef, it.bounds(),
                          edgeKind);
          }
        }

        hookupOutputPort(valueSymbol, it.bounds(),
                         {{.node = &stateNode, .lsp = nullptr}}, edgeKind);
      }

      auto symRef = toSymbolRef(*symbol);
      for (auto const &driver : driverList) {
        if (driver.node == nullptr) {
          continue;
        }

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
  ast::ValuePath::visitPaths(
      *expr, evalCtx, [&](const ast::ValuePath &path) -> void {
        if (path.empty() || !path.lsp) {
          return;
        }
        auto const *rootSymbol = path.rootSymbol();
        if (rootSymbol == nullptr) {
          return;
        }
        auto const &symbol = *rootSymbol;
        auto const &lsp = *path.lsp;

        DEBUG_PRINT("Resolved LSP in port connection expression: {} {} "
                    "bounds={}, loc={}\n",
                    toString(symbol.kind), symbol.name,
                    toString(path.lspBounds),
                    Utilities::locationStr(compilation, symbol.location));

        for (auto *node : portNodes) {
          auto driverBounds = DriverBitRange(path.lspBounds);
          if (isOutput) {
            // If lvalue, then the port defines symbol with bounds.
            // FIXME: *Merge* the driver — there is currently no way to tell
            // what bounds the LSP occupies within the port type and to drive
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
    for (auto const *driver : drivers) {
      auto bounds = driver->getBounds();

      DEBUG_PRINT("{} driven by prefix={}\n", toString(bounds),
                  getDriverPathName(valueSymbol, *driver));

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
        for (auto const *driver : drivers) {
          auto bounds = driver->getBounds();

          DEBUG_PRINT("[{}:{}] driven by prefix={}\n", bounds.first,
                      bounds.second, getDriverPathName(symbol, *driver));

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
  mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
               dfa->getState().valueDrivers, edgeKind);
}

void NetlistBuilder::handleContinuousAssign(
    ast::ContinuousAssignSymbol const &symbol) {
  DEBUG_PRINT("ContinuousAssign\n");
  auto dfa = std::make_shared<DataFlowAnalysis>(analysisManager, symbol, *this);
  dfa->run(symbol.getAssignment());
  mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
               dfa->getState().valueDrivers, ast::EdgeKind::None);
}

void NetlistBuilder::handle(ast::GenerateBlockSymbol const &symbol) {
  if (!symbol.isUninstantiated) {
    visitMembers(symbol);
  }
}

} // namespace slang::netlist
