#include "netlist/NetlistBuilder.hpp"
#include "netlist/ReportingUtilities.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/HierarchicalReference.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/expressions/MiscExpressions.h"

#include <ranges>

namespace slang::netlist {

NetlistBuilder::NetlistBuilder(ast::Compilation &compilation,
                               NetlistGraph &graph)
    : compilation(compilation), graph(graph) {
  NetlistNode::nextID = 0; // Reset the static ID counter.
}

void NetlistBuilder::finalize() { processPendingRvalues(); }

/// Apply an outer select expression to a connection expression. Return a
/// pointer to the new expression, or nullptr if no outer select was found.
auto applySelectToConnExpr(BumpAllocator &alloc,
                           ast::Expression const &connectionExpr,
                           ast::Expression const &lsp)
    -> const ast::Expression * {
  const ast::Expression *initialLSP = nullptr;
  switch (lsp.kind) {
  case ast::ExpressionKind::ElementSelect: {
    auto &es = lsp.as<ast::ElementSelectExpression>();
    initialLSP = alloc.emplace<ast::ElementSelectExpression>(
        *es.type, const_cast<ast::Expression &>(connectionExpr), es.selector(),
        es.sourceRange);
    break;
  }
  case ast::ExpressionKind::RangeSelect: {
    auto &rs = lsp.as<ast::RangeSelectExpression>();
    initialLSP = alloc.emplace<ast::RangeSelectExpression>(
        rs.getSelectionKind(), *rs.type,
        const_cast<ast::Expression &>(connectionExpr), rs.left(), rs.right(),
        rs.sourceRange);
    break;
  }
  case ast::ExpressionKind::MemberAccess: {
    auto &ma = lsp.as<ast::MemberAccessExpression>();
    initialLSP = alloc.emplace<ast::MemberAccessExpression>(
        *ma.type, const_cast<ast::Expression &>(connectionExpr), ma.member,
        ma.sourceRange);
    break;
  }
  default:
    break;
  }
  return initialLSP;
}

void NetlistBuilder::_resolveInterfaceRef(
    std::vector<InterfaceVarBounds> result, ast::EvalContext &evalCtx,
    ast::ModportPortSymbol const &symbol, ast::Expression const &lsp) {

  // The aim is to translate references to the modport ports found in
  // in expressions, via their connection expressions. Follow modport
  // connections to arrive at the base interface. The underlying symbol can then
  // be resolved, allows inputs to be matched with outputs and vice versa.

  BumpAllocator alloc;

  auto loc = ReportingUtilities::locationStr(compilation, symbol.location);

  DEBUG_PRINT("Resolving interface references for symbol {} {} loc={}\n",
              toString(symbol.kind), symbol.name, loc);

  if (auto expr = symbol.getConnectionExpr()) {

    // Apply any outer select expressions to the connection expression.
    auto initialLSP = applySelectToConnExpr(alloc, *expr, lsp);

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
          DEBUG_PRINT("Resolved LSP in modport connection expression: {} {} "
                      "bounds=[{}:{}] loc={}\n",
                      toString(symbol.kind), symbol.name, bounds->first,
                      bounds->second, loc);

          if (symbol.kind == ast::SymbolKind::Variable) {
            // This is an interface variable.

          } else if (symbol.kind == ast::SymbolKind::ModportPort) {
            // Recurse to follow a nested modport connection.
            _resolveInterfaceRef(result, evalCtx,
                                 symbol.as<ast::ModportPortSymbol>(), lsp);
          } else {
            DEBUG_PRINT("Unhandled symbol of kind {}\n", toString(symbol.kind));
            SLANG_UNREACHABLE;
          }
        },
        initialLSP);
  }
}

auto NetlistBuilder::resolveInterfaceRef(ast::EvalContext &evalCtx,
                                         ast::ModportPortSymbol const &symbol,
                                         ast::Expression const &lsp)
    -> std::vector<InterfaceVarBounds> {
  std::vector<InterfaceVarBounds> result;
  _resolveInterfaceRef(result, evalCtx, symbol, lsp);
  return result;
}

void NetlistBuilder::addRvalue(ast::EvalContext &evalCtx,
                               ast::ValueSymbol const &symbol,
                               ast::Expression const &lsp,
                               DriverBitRange bounds, NetlistNode *node) {
  DEBUG_PRINT("Adding pending R-value: {} [{}:{}]\n", symbol.name, bounds.first,
              bounds.second);

  // For rvalues that are via a modport port, resolve the interface variables
  // they are driven from and add dependencies from them. This is similar to the
  // way that ports are handled.
  if (symbol.kind == ast::SymbolKind::ModportPort) {
    auto vars =
        resolveInterfaceRef(evalCtx, symbol.as<ast::ModportPortSymbol>(), lsp);
    return;
  }

  // Add to the pending list to be processed later.
  pendingRValues.emplace_back(&symbol, &lsp, bounds, node);
}

void NetlistBuilder::processPendingRvalues() {
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
        auto &edge = graph.addEdge(*source.node, *pending.node);
        edge.setVariable(pending.symbol, pending.bounds);
        DEBUG_PRINT("Added edge from driver node {} to R-value node {}\n",
                    source.node->ID, pending.node->ID);
      }
    }
  }
  pendingRValues.clear();
}

void NetlistBuilder::hookupOutputPort(ast::ValueSymbol const &symbol,
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
        graph.addEdge(*driver.node, *portNode).setVariable(&symbol, bounds);

        DEBUG_PRINT("Adding port dependency for symbol {} to port {}\n",
                    symbol.name, portSymbol->name);
      }
    }
  }
}

void NetlistBuilder::mergeProcDrivers(ast::EvalContext &evalCtx,
                                      SymbolTracker const &symbolTracker,
                                      SymbolDrivers const &symbolDrivers,
                                      ast::EdgeKind edgeKind) {
  DEBUG_PRINT("Merging procedural drivers\n");

  for (auto [symbol, index] : symbolTracker) {
    DEBUG_PRINT("Symbol {} at index={}\n", symbol->name, index);

    if (index >= symbolDrivers.size()) {
      // No drivers for this symbol so we don't need to do anything.
      continue;
    }

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

        // Sequential edge, so the procedural drivers act on a stateful
        // variable which is represented by a node in the graph. We create
        // this node, add edges from the procedural drivers to it, and then
        // add the state node as the new driver for the range.

        auto &stateNode = graph.addNode(std::make_unique<State>(
            &symbol->as<ast::ValueSymbol>(), it.bounds()));

        for (auto &driver : driverList) {
          graph.addEdge(*driver.node, stateNode)
              .setVariable(symbol, it.bounds());
        }

        addDriver(*symbol, nullptr, it.bounds(), &stateNode);

        hookupOutputPort(symbol->as<ast::ValueSymbol>(), it.bounds(),
                         {{&stateNode, nullptr}});
      }

      // Find drivers that are modport ports and resolve the interface variables
      // that they drive.
      for (auto &driver : driverList) {
        if (symbol->kind == ast::SymbolKind::ModportPort) {
          auto vars = resolveInterfaceRef(
              evalCtx, symbol->as<ast::ModportPortSymbol>(), *driver.lsp);
        }
      }
    }
  }
}

} // namespace slang::netlist
