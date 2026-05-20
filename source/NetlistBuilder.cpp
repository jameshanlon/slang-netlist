#include "NetlistBuilder.hpp"
#include "BitSliceList.hpp"
#include "DataFlowAnalysis.hpp"
#include "common/Wildcard.hpp"

#include "common/Utilities.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/HierarchicalReference.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"

#include "slang/util/FlatMap.h"

namespace slang::netlist {

namespace {

/// Thread-local cache mapping AST symbols to their interned
/// SymbolReference pointer. Populated lazily by toSymbolRef() to avoid
/// repeated hierarchicalPath string construction and SymbolTable lookups.
/// It is cleared at the start of each parallel task and at the start of
/// each sequential build() so stale entries never leak.
thread_local flat_hash_map<const ast::Symbol *, SymbolReference const *>
    threadLocalSymbolRefCache;

} // namespace

void NetlistBuilder::clearThreadLocalSymbolRefCache() {
  threadLocalSymbolRefCache.clear();
}

NetlistBuilder::NetlistBuilder(ast::Compilation &compilation,
                               analysis::AnalysisManager &analysisManager,
                               NetlistGraph &graph, BuilderOptions options)
    : compilation(compilation), analysisManager(analysisManager), graph(graph),
      options(options) {
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
    -> SymbolReference const * {
  auto it = threadLocalSymbolRefCache.find(&sym);
  if (it != threadLocalSymbolRefCache.end()) {
    return it->second;
  }
  auto const *ref = graph.symbolTable.intern(
      sym.name, sym.getHierarchicalPath(), toTextLocation(sym.location));
  threadLocalSymbolRefCache.emplace(&sym, ref);
  return ref;
}

void NetlistBuilder::build(const ast::Symbol &root) { pipeline.run(root); }

void NetlistBuilder::finalize() { pipeline.finalize(); }

void NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target) {
  source.addEdge(target);
}

void NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target,
                                   SymbolReference const *symbol,
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
              toString(edgeKind), source.ID, target.ID,
              symbol != nullptr ? symbol->hierarchicalPath : std::string{},
              toString(edgeBounds));

  auto &edge = source.addEdge(target);
  if (!edge.setVariable(symbol, edgeBounds)) {
    // Existing edge carries a non-contiguous range for the same symbol;
    // create a parallel edge to preserve exact bit-range accuracy.
    auto &newEdge = source.addNewEdge(target);
    newEdge.setVariable(symbol, edgeBounds);
    newEdge.setEdgeKind(edgeKind);
  } else {
    edge.setEdgeKind(edgeKind);
  }
}

auto NetlistBuilder::getDriverPathName(ast::ValueSymbol const &symbol,
                                       analysis::ValueDriver const &driver)
    -> std::string {
  ast::EvalContext evalContext(symbol);
  return driver.path.toString(evalContext);
}

auto NetlistBuilder::collectSensitivity(
    ast::ProceduralBlockSymbol const &symbol) -> SmallVector<SensitivityEntry> {
  SmallVector<SensitivityEntry> result;

  if (symbol.procedureKind != ast::ProceduralBlockKind::AlwaysFF &&
      symbol.procedureKind != ast::ProceduralBlockKind::Always) {
    return result;
  }

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

  auto const &timing = symbol.getBody().as<ast::TimedStatement>().timing;

  bool sawCombEvent = false;

  auto recordEvent = [&](ast::SignalEventControl const &sec) {
    // Any unqualified event (`always @(x or y)`) demotes the block to
    // combinational.
    if (sec.edge == ast::EdgeKind::None) {
      sawCombEvent = true;
      return;
    }
    // Skip non-symbol event expressions; the State node still captures
    // the sequential nature.
    if (ast::ValueExpressionBase::isKind(sec.expr.kind)) {
      auto const &valExpr = sec.expr.as<ast::ValueExpressionBase>();
      result.push_back({&valExpr.symbol, &sec.expr, sec.edge});
    }
  };

  if (timing.kind == ast::TimingControlKind::SignalEvent) {
    recordEvent(timing.as<ast::SignalEventControl>());
  } else if (timing.kind == ast::TimingControlKind::EventList) {
    for (auto const *e : timing.as<ast::EventListControl>().events) {
      recordEvent(e->as<ast::SignalEventControl>());
    }
  }

  if (sawCombEvent) {
    return {};
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

void NetlistBuilder::addDriversToNode(DriverList const &drivers,
                                      NetlistNode &node,
                                      SymbolReference const *symbol,
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

  pendingQueue.enqueue(symbol, lsp, bounds, node);
}

void NetlistBuilder::hookupOutputPort(ast::ValueSymbol const &symbol,
                                      DriverBitRange bounds,
                                      DriverList const &driverList) {

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
          addDependency(*driver.node, *portNode, symRef, bounds);
        }
      }
    }
  }
}

void NetlistBuilder::mergeDrivers(
    ast::EvalContext &evalCtx, ValueTracker const &valueTracker,
    ValueDrivers const &valueDrivers,
    std::span<SensitivityEntry const> sensitivity) {
  DEBUG_PRINT("Merging procedural drivers\n");

  bool const isSequential = !sensitivity.empty();

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

      if (!isSequential) {

        // Combinational block, so just add the interval with the driving
        // node(s).
        mergeDrivers(*symbol, it.bounds(), driverList);

        hookupOutputPort(valueSymbol, it.bounds(), driverList);

      } else {

        // Sequential: create a State node, wire data drivers to it, and
        // attach a clock edge per sensitivity entry. The State becomes
        // the new driver for the range.

        auto &stateNode = nodeFactory.createState(valueSymbol, it.bounds());

        auto symRef = toSymbolRef(*symbol);
        for (auto const &driver : driverList) {
          if (driver.node != nullptr) {
            addDependency(*driver.node, stateNode, symRef, it.bounds());
          }
        }

        // Route each clock through the pending-rvalue resolver so input
        // ports, internal wires, gated clocks, and clock dividers all
        // hook up via the same driver-walk used for ordinary r-values.
        for (auto const &entry : sensitivity) {
          auto width = entry.signal->getType().getSelectableWidth();
          if (width == 0) {
            continue;
          }
          DriverBitRange sigBounds{0, static_cast<int32_t>(width - 1)};
          pendingQueue.enqueue(*entry.signal, *entry.lsp, sigBounds, &stateNode,
                               entry.edgeKind);
        }

        hookupOutputPort(valueSymbol, it.bounds(),
                         {{.node = &stateNode, .lsp = nullptr}});
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

void NetlistBuilder::handle(ast::PortSymbol const &symbol) {
  DEBUG_PRINT("PortSymbol {}\n", symbol.name);
  portHandler.materializePortNodes(symbol);
}

void NetlistBuilder::handle(ast::VariableSymbol const &symbol) {

  // Identify interface variables.
  if (auto const *scope = symbol.getParentScope()) {
    auto const *container = scope->getContainingInstance();
    if (container != nullptr && container->parentInstance != nullptr) {
      if (container->parentInstance->isInterface()) {
        DEBUG_PRINT("Interface variable {}\n", symbol.name);

        // Same canonical-body redirect as for port internals; see
        // materializePortNodes for the rationale.
        auto const &driverQuerySymbol =
            canonicalResolver.getCanonicalValueSymbol(symbol);
        auto drivers = analysisManager.getDrivers(driverQuerySymbol);
        for (auto const *driver : drivers) {
          auto bounds = driver->getBounds();

          DEBUG_PRINT("[{}:{}] driven by prefix={}\n", bounds.first,
                      bounds.second, getDriverPathName(symbol, *driver));

          // Create a variable node for the interface member's driven range.
          nodeFactory.createVariable(symbol, DriverBitRange(bounds));
        }
      }
    }
  }
}

bool NetlistBuilder::isBlackBoxInstance(
    ast::InstanceSymbol const &symbol) const {
  if (options.blackBoxes.empty()) {
    return false;
  }
  auto const defName = std::string(symbol.getDefinition().name);
  for (auto const &pattern : options.blackBoxes) {
    if (wildcardMatch(defName.c_str(), pattern.c_str())) {
      return true;
    }
  }
  auto const path = symbol.getHierarchicalPath();
  for (auto const &pattern : options.blackBoxes) {
    if (wildcardMatch(path.c_str(), pattern.c_str())) {
      return true;
    }
  }
  return false;
}

void NetlistBuilder::handle(ast::InstanceSymbol const &symbol) {
  DEBUG_PRINT("InstanceSymbol {}\n", symbol.name);

  if (symbol.body.flags.has(ast::InstanceFlags::Uninstantiated)) {
    return;
  }

  // Record cuts before body.visit / port-node materialization so the
  // formal port nodes are split on the same cut grid the parent's
  // concat-shaped actuals expect.
  if (options.propCutsAcrossPorts) {
    portHandler.recordCutsFromPortConnections(symbol);
  }

  bool const blackBox = isBlackBoxInstance(symbol);

  if (blackBox) {
    DEBUG_PRINT("Black-boxing instance {} ({})\n", symbol.name,
                symbol.getDefinition().name);
    // Materialize port nodes without descending into the body, so the
    // parent's port wiring has somewhere to terminate but no internal
    // logic contributes nodes or edges.
    for (auto const &member : symbol.body.members()) {
      if (member.kind == ast::SymbolKind::Port) {
        portHandler.materializePortNodes(member.as<ast::PortSymbol>());
      }
    }
  } else {
    symbol.body.visit(*this);
  }

  for (auto const *portConnection : symbol.getPortConnections()) {

    if (portConnection->port.kind == ast::SymbolKind::Port) {
      portHandler.handlePortConnection(symbol, *portConnection);
    } else if (portConnection->port.kind == ast::SymbolKind::InterfacePort) {
      // Interfaces are handled via ModportPorts.
    } else {
      SLANG_UNREACHABLE;
    }
  }
}

void NetlistBuilder::handle(ast::ProceduralBlockSymbol const &symbol) {
  // handle() is only called during Phase 1 (the collecting traversal),
  // so always defer — Phase 2 dispatches via handleProceduralBlock().
  SLANG_ASSERT(pipeline.isCollecting());
  pipeline.deferBlock(symbol, /*isProcedural=*/true);
}

void NetlistBuilder::handle(ast::ContinuousAssignSymbol const &symbol) {
  // handle() is only called during Phase 1 (the collecting traversal),
  // so always defer — Phase 2 dispatches via handleContinuousAssign().
  SLANG_ASSERT(pipeline.isCollecting());
  pipeline.deferBlock(symbol, /*isProcedural=*/false);
}

void NetlistBuilder::handleProceduralBlock(
    ast::ProceduralBlockSymbol const &symbol) {
  DEBUG_PRINT("ProceduralBlock\n");
  auto sensitivity = collectSensitivity(symbol);
  auto dfa = std::make_shared<DataFlowAnalysis>(analysisManager, symbol, *this);
  dfa->run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
  dfa->finalize();
  mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
               dfa->getState().valueDrivers, sensitivity);
}

void NetlistBuilder::handleContinuousAssign(
    ast::ContinuousAssignSymbol const &symbol) {
  DEBUG_PRINT("ContinuousAssign\n");
  auto dfa = std::make_shared<DataFlowAnalysis>(analysisManager, symbol, *this);
  dfa->run(symbol.getAssignment());
  mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
               dfa->getState().valueDrivers);
}

void NetlistBuilder::handle(ast::GenerateBlockSymbol const &symbol) {
  if (!symbol.isUninstantiated) {
    visitMembers(symbol);
  }
}

} // namespace slang::netlist
