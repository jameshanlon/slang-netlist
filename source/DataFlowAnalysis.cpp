#include "netlist/DataFlowAnalysis.hpp"

namespace slang::netlist {

void DataFlowAnalysis::addNonBlockingLvalue(ast::ValueSymbol const &symbol,
                                            ast::Expression const &lsp,
                                            DriverBitRange bounds,
                                            NetlistNode *node) {
  DEBUG_PRINT("Adding pending non-blocking L-value: {} [{}:{}]\n", symbol.name,
              bounds.first, bounds.second);
  pendingLValues.emplace_back(&symbol, &lsp, bounds, node);
}

void DataFlowAnalysis::processNonBlockingLvalues() {
  for (auto &pending : pendingLValues) {
    DEBUG_PRINT("Processing pending non-blocking L-value: {} [{}:{}]\n",
                pending.symbol->name, pending.bounds.first,
                pending.bounds.second);
    driverMap.addDriver(getState().definitions, *pending.symbol, pending.lsp,
                        pending.bounds, pending.node);
  }
  pendingLValues.clear();
}

void DataFlowAnalysis::handleRvalue(ast::ValueSymbol const &symbol,
                                    ast::Expression const &lsp,
                                    DriverBitRange bounds) {
  DEBUG_PRINT("Handle R-value: {} [{}:{}]\n", symbol.name, bounds.first,
              bounds.second);

  //// Initiliase a new interval map for the R-value to track
  //// which parts of it have been assigned within this procedural block.
  // SymbolDriverMap rvalueMap;
  // BumpAllocator ba;
  // SymbolDriverMap::allocator_type alloc(ba);

  // rvalueMap.insert(bounds, {nullptr, nullptr}, alloc);

  // if (symbolToSlot.contains(&symbol)) {

  //  // Symbol is assigned in this procedural block.
  //  auto &currState = getState();
  //  auto index = symbolToSlot.at(&symbol);

  //  if (currState.definitions.size() <= index) {
  //    // There are no definitions for this symbol on the current control path,
  //    // but definition(s) do exist on other control paths. This occurs when
  //    // the symbol is sequential and the definition is created on a previous
  //    // edge (ie sequential).
  //    DEBUG_PRINT("No definition for symbol {} at index {}, adding to "
  //                "pending list.\n",
  //                symbol.name, index);
  //    graph.addRvalue(symbol, lsp, bounds, currState.node);
  //    return;
  //  }

  //  auto &definitions = currState.definitions[index];
  //  for (auto it = definitions.find(bounds); it != definitions.end(); it++) {

  //    auto itBounds = it.bounds();
  //    auto &currState = getState();

  //    // Definition bounds completely contains R-value bounds.
  //    // Ie. the definition covers the R-value.
  //    if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {

  //      // Add an edge from the definition node to the current node
  //      // using it.
  //      if (currState.node) {
  //        auto &edge = graph.addEdge(*(*it).node, *currState.node);
  //        edge.setVariable(&symbol, bounds);
  //      }

  //      // All done, exit early.
  //      return;
  //    }

  //    // R-value bounds completely contain a definition bounds.
  //    // Ie. a definition contributes to the R-value.

  //    if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {

  //      // Add an edge from the definition node to the current node
  //      // using it.
  //      SLANG_ASSERT(currState.node);
  //      auto &edge = graph.addEdge(*(*it).node, *currState.node);
  //      edge.setVariable(&symbol, bounds);
  //    }
  //  }

  //  // Calculate the difference between the R-value map and the
  //  // definitions provided in this procedural block. That leaves the
  //  // parts of the R-value that are defined outside of this procedural
  //  // block.
  //  rvalueMap = IntervalMapUtils::difference(rvalueMap, definitions, alloc);
  //}

  //// If we get to this point, rvalueMap hold the intervals of the R-value
  //// that are assigned outside of this procedural block.  In this case, we
  //// just add a pending R-value to the list of pending R-values to be
  //// processed after all drivers have been visited.

  // auto &currState = getState();
  // auto *node = currState.node != nullptr ? currState.node : externalNode;

  // for (auto it = rvalueMap.begin(); it != rvalueMap.end(); ++it) {
  //   auto itBounds = it.bounds();
  //   graph.addRvalue(symbol, lsp, {itBounds.first, itBounds.second}, node);
  // }
}

void DataFlowAnalysis::finalize() { processNonBlockingLvalues(); }

void DataFlowAnalysis::handleLvalue(const ast::ValueSymbol &symbol,
                                    const ast::Expression &lsp,
                                    DriverBitRange bounds) {
  DEBUG_PRINT("Handle lvalue: {} [{}:{}]\n", symbol.name, bounds.first,
              bounds.second);

  // If this is a non-blocking assignment, then the assignment occurs at the
  // end of the block and so the result is not visible within the block.
  // However, the definition may still be used in the block as an initial
  // R-value.

  if (!isBlocking) {
    addNonBlockingLvalue(symbol, lsp, bounds, getState().node);
    return;
  }

  driverMap.addDriver(getState().definitions, symbol, &lsp, bounds,
                      getState().node);
}

/// As per DataFlowAnalysis in upstream slang, but with custom handling of
/// L- and R-values. Called by the LSP visitor.
void DataFlowAnalysis::noteReference(const ast::ValueSymbol &symbol,
                                     const ast::Expression &lsp) {

  // This feels icky but we don't count a symbol as being referenced in
  // the procedure if it's only used inside an unreachable flow path. The
  // alternative would just frustrate users, but the reason it's icky is
  // because whether a path is reachable is based on whatever level of
  // heuristics we're willing to implement rather than some well defined
  // set of rules in the LRM.

  auto &currState = getState();

  if (!currState.reachable) {
    return;
  }

  auto bounds =
      ast::LSPUtilities::getBounds(lsp, getEvalContext(), symbol.getType());

  if (!bounds) {
    // This probably cannot be hit given that we early out elsewhere for
    // invalid expressions.
    return;
  }

  if (isLValue) {
    handleLvalue(symbol, lsp, *bounds);
  } else {
    handleRvalue(symbol, lsp, *bounds);
  }
}

void DataFlowAnalysis::updateNode(NetlistNode *node, bool conditional) {
  auto &currState = getState();

  // If there is a previous conditional node, then add an edge
  if (currState.condition) {
    graph.addEdge(*currState.condition, *node);
  }

  // If the new node is a conditional, then
  if (conditional) {
    currState.condition = node;
  } else {
    currState.condition = nullptr;
  }

  // Set the new current node.
  currState.node = node;
}

void DataFlowAnalysis::handle(const ast::ProceduralAssignStatement &stmt) {
  // Procedural force statements don't act as drivers of their lvalue
  // target.
  if (stmt.isForce) {
    prohibitLValue = true;
    visitStmt(stmt);
    prohibitLValue = false;
  } else {
    visitStmt(stmt);
  }
}

void DataFlowAnalysis::handle(const ast::AssignmentExpression &expr) {
  DEBUG_PRINT("AssignmentExpression\n");

  auto &node = graph.addNode(std::make_unique<Assignment>(expr));

  updateNode(&node, false);

  // Note that this method mirrors the logic in the base class
  // handler but we need to track the LValue status of the lhs.
  if (!prohibitLValue) {
    SLANG_ASSERT(!isLValue);
    isLValue = true;
    isBlocking = expr.isBlocking();
    visit(expr.left());
    isLValue = false;
  } else {
    visit(expr.left());
  }

  if (!expr.isLValueArg()) {
    visit(expr.right());
  }
}

void DataFlowAnalysis::handle(ast::ConditionalStatement const &stmt) {
  DEBUG_PRINT("ConditionalStatement\n");

  // If all conditions are constant, then there is no need to include this
  if (std::all_of(stmt.conditions.begin(), stmt.conditions.end(),
                  [&](ast::ConditionalStatement::Condition const &cond) {
                    return tryEvalBool(*cond.expr);
                  })) {
    visitStmt(stmt);
    return;
  }

  auto &node = graph.addNode(std::make_unique<Conditional>(stmt));
  updateNode(&node, true);
  visitStmt(stmt);
}

void DataFlowAnalysis::handle(ast::CaseStatement const &stmt) {
  DEBUG_PRINT("CaseStatement\n");
  auto &node = graph.addNode(std::make_unique<Case>(stmt));
  updateNode(&node, true);
  visitStmt(stmt);
}

AnalysisState DataFlowAnalysis::mergeStates(const AnalysisState &a,
                                            const AnalysisState &b) {
  AnalysisState result;

  // TODO: the operation to merge drivers between the two states can be
  // optimized by performing a linear iteration through both maps, rather than
  // adding each b interval separately.

  // Copy a's definitions as the base.
  for (auto i = 0; i < a.definitions.size(); i++) {
    result.definitions.emplace_back(
        a.definitions[i].clone(driverMap.getAllocator()));
  }

  // Merge in b's definitions.
  for (auto i = 0; i < b.definitions.size(); i++) {
    DEBUG_PRINT("Merging symbol at index {}\n", i);
    auto *symbol = driverMap.getSymbol(i);
    for (auto it = b.definitions[i].begin(); it != b.definitions[i].end();
         it++) {
      auto bounds = it.bounds();
      auto &driverList = b.definitions[i].getDriverList(*it);
      DEBUG_PRINT("Inserting b bounds [{}:{}]\n", bounds.first, bounds.second);
      driverMap.mergeDrivers(result.definitions, *symbol, bounds, driverList);
    }
  }

  auto mergeNodes = [&](NetlistNode *a, NetlistNode *b) -> NetlistNode * {
    if (a && b) {

      // If the nodes are different, then we need to create a new
      // node.
      if (a != b) {
        auto &node = graph.addNode(std::make_unique<Merge>());
        graph.addEdge(*a, node);
        graph.addEdge(*b, node);
        return &node;
      }

      return a;

    } else if (a && b == nullptr) {

      // Otherwise, just use a node.
      return a;

    } else if (b && a == nullptr) {

      // Otherwise, just use b node.
      return b;

    } else {

      // If both nodes are null, then we don't need to set the node.
      return nullptr;
    }
  };

  // Node pointers.
  result.node = mergeNodes(a.node, b.node);
  result.condition = mergeNodes(a.condition, b.condition);

  // Reachable.
  result.reachable = a.reachable;

  DEBUG_PRINT("Merged states: a.defs.size={}, b.defs.size={}, "
              "result.defs.size={}\n",
              a.definitions.size(), b.definitions.size(),
              result.definitions.size());

  return result;
}

void DataFlowAnalysis::joinState(AnalysisState &result,
                                 const AnalysisState &other) {
  DEBUG_PRINT("joinState\n");
  if (result.reachable == other.reachable) {
    result = mergeStates(result, other);
  } else if (!result.reachable) {
    result = copyState(other);
  }
}

void DataFlowAnalysis::meetState(AnalysisState &result,
                                 const AnalysisState &other) {
  DEBUG_PRINT("meetState\n");
  if (!other.reachable) {
    result.reachable = false;
    return;
  }
  result = mergeStates(result, other);
}

AnalysisState DataFlowAnalysis::copyState(const AnalysisState &source) {
  DEBUG_PRINT("copyState\n");
  AnalysisState result;
  result.reachable = source.reachable;
  result.node = source.node;
  result.condition = source.condition;
  result.definitions.reserve(source.definitions.size());
  for (const auto &definition : source.definitions) {
    result.definitions.emplace_back(definition.clone(driverMap.getAllocator()));
  }
  return result;
}

AnalysisState DataFlowAnalysis::unreachableState() {
  DEBUG_PRINT("unreachableState\n");
  AnalysisState result;
  result.reachable = false;
  return result;
}

AnalysisState DataFlowAnalysis::topState() { return {}; }

} // namespace slang::netlist
