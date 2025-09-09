#include "netlist/DataFlowAnalysis.hpp"

namespace slang::netlist {

void DataFlowAnalysis::updateDefinitions(const ast::ValueSymbol &symbol,
                                         std::pair<uint64_t, uint64_t> bounds,
                                         NetlistNode *node) {
  auto &currState = getState();
  auto [it, inserted] =
      symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());
  auto index = it->second;
  if (index >= currState.definitions.size()) {
    currState.definitions.resize(index + 1);
  }
  if (index >= slotToSymbol.size()) {
    slotToSymbol.resize(index + 1);
    slotToSymbol[index] = &symbol;
  }
  auto &definitions = currState.definitions[index];
  for (auto it = definitions.find(bounds); it != definitions.end();) {
    auto itBounds = it.bounds();
    if (ConstantRange(itBounds).overlaps(ConstantRange(bounds))) {
      definitions.erase(it, bitMapAllocator);
      if (itBounds.first < bounds.first) {
        definitions.insert({itBounds.first, bounds.first - 1}, *it,
                           bitMapAllocator);
      }
      if (itBounds.second > bounds.second) {
        definitions.insert({bounds.second + 1, itBounds.second}, *it,
                           bitMapAllocator);
      }
      it = definitions.find(bounds);
      continue;
    }
    if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {
      definitions.erase(it, bitMapAllocator);
      it = definitions.find(bounds);
      continue;
    }
    ++it;
  }
  definitions.insert(bounds, node, bitMapAllocator);
}

void DataFlowAnalysis::addNonBlockingLvalue(
    const ast::ValueSymbol *symbol, std::pair<uint64_t, uint64_t> bounds,
    NetlistNode *node) {
  DEBUG_PRINT("Adding pending non-blocking L-value: {} [{}:{}]\n", symbol->name,
              bounds.first, bounds.second);
  SLANG_ASSERT(symbol != nullptr && "Symbol must not be null");
  pendingLValues.emplace_back(symbol, bounds, node);
}

void DataFlowAnalysis::processNonBlockingLvalues() {
  for (auto &pending : pendingLValues) {
    DEBUG_PRINT("Processing pending non-blocking L-value: {} [{}:{}]\n",
                pending.symbol->name, pending.bounds.first,
                pending.bounds.second);
    updateDefinitions(*pending.symbol, pending.bounds, pending.node);
  }
  pendingLValues.clear();
}

void DataFlowAnalysis::handleRvalue(const ast::ValueSymbol &symbol,
                                    std::pair<uint32_t, uint32_t> bounds) {
  DEBUG_PRINT("Handle R-value: {} [{}:{}]\n", symbol.name, bounds.first,
              bounds.second);
  IntervalMap<uint64_t, NetlistNode *, 8> rvalueMap;
  BumpAllocator ba;
  IntervalMap<int32_t, int32_t>::allocator_type alloc(ba);
  rvalueMap.insert(bounds, nullptr, alloc);
  if (symbolToSlot.contains(&symbol)) {
    auto &currState = getState();
    auto index = symbolToSlot.at(&symbol);
    if (currState.definitions.size() <= index) {
      DEBUG_PRINT(
          "No definition for symbol {} at index {}, adding to pending list.\n",
          symbol.name, index);
      graph.addRvalue(&symbol, bounds, currState.node);
      return;
    }
    auto &definitions = currState.definitions[index];
    for (auto it = definitions.find(bounds); it != definitions.end(); it++) {
      auto itBounds = it.bounds();
      auto &currState = getState();
      if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {
        if (currState.node) {
          auto &edge = graph.addEdge(**it, *currState.node);
          edge.setVariable(&symbol, bounds);
        }
        return;
      }
      if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {
        SLANG_ASSERT(currState.node);
        auto &edge = graph.addEdge(**it, *currState.node);
        edge.setVariable(&symbol, bounds);
      }
    }
    rvalueMap = IntervalMapUtils::difference(rvalueMap, definitions, alloc);
  }
  auto &currState = getState();
  auto *node = currState.node != nullptr ? currState.node : externalNode;
  for (auto it = rvalueMap.begin(); it != rvalueMap.end(); ++it) {
    auto itBounds = it.bounds();
    graph.addRvalue(&symbol, {itBounds.first, itBounds.second}, node);
  }
}

void DataFlowAnalysis::finalize() { processNonBlockingLvalues(); }

void DataFlowAnalysis::handleLvalue(const ast::ValueSymbol &symbol,
                                    const ast::Expression &lsp,
                                    std::pair<uint32_t, uint32_t> bounds) {
  DEBUG_PRINT("Handle lvalue: {} [{}:{}]\n", symbol.name, bounds.first,
              bounds.second);
  if (!isBlocking) {
    addNonBlockingLvalue(&symbol, bounds, getState().node);
    return;
  }
  updateDefinitions(symbol, bounds, getState().node);
}

void DataFlowAnalysis::noteReference(const ast::ValueSymbol &symbol,
                                     const ast::Expression &lsp) {
  auto &currState = getState();
  if (!currState.reachable) {
    return;
  }
  auto bounds =
      ast::LSPUtilities::getBounds(lsp, getEvalContext(), symbol.getType());
  if (!bounds) {
    return;
  }
  if (isLValue) {
    handleLvalue(symbol, lsp, *bounds);
  } else {
    handleRvalue(symbol, *bounds);
  }
}

// Template and overloads for handle
// ...existing code...

void DataFlowAnalysis::updateNode(NetlistNode *node, bool conditional) {
  auto &currState = getState();
  if (currState.condition) {
    graph.addEdge(*currState.condition, *node);
  }
  if (conditional) {
    currState.condition = node;
  } else {
    currState.condition = nullptr;
  }
  currState.node = node;
}

void DataFlowAnalysis::handle(const ast::ProceduralAssignStatement &stmt) {
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
  auto symbolCount = std::max(a.definitions.size(), b.definitions.size());
  result.definitions.resize(symbolCount);
  for (size_t i = 0; i < symbolCount; i++) {
    DEBUG_PRINT("Merging symbol at index {}\n", i);
    if (i >= a.definitions.size() && i < b.definitions.size()) {
      result.definitions[i] = b.definitions[i].clone(bitMapAllocator);
      continue;
    }
    if (i >= b.definitions.size() && i < a.definitions.size()) {
      result.definitions[i] = a.definitions[i].clone(bitMapAllocator);
      continue;
    }
    SLANG_ASSERT(i < a.definitions.size());
    SLANG_ASSERT(i < b.definitions.size());
    auto aIt = a.definitions[i].begin();
    auto bIt = b.definitions[i].begin();
    while (aIt != a.definitions[i].end() && bIt != b.definitions[i].end()) {
      DEBUG_PRINT("Merging intervals {} a=[{}:{}], b=[{}:{}]\n",
                  slotToSymbol[i]->name, aIt.bounds().first,
                  aIt.bounds().second, bIt.bounds().first, bIt.bounds().second);
      auto aBounds = aIt.bounds();
      auto bBounds = bIt.bounds();
      if (aBounds == bBounds) {
        auto &node = graph.addNode(std::make_unique<Merge>());
        if (*aIt) {
          auto &edgea = graph.addEdge(**aIt, node);
          edgea.setVariable(slotToSymbol[i], aBounds);
        }
        if (*bIt) {
          auto &edgeb = graph.addEdge(**bIt, node);
          edgeb.setVariable(slotToSymbol[i], bBounds);
        }
        result.definitions[i].insert(aBounds, &node, bitMapAllocator);
        ++aIt;
        ++bIt;
      } else if (ConstantRange(aBounds).overlaps(ConstantRange(bBounds))) {
        if (aBounds.first < bBounds.first) {
          result.definitions[i].insert({aBounds.first, bBounds.first}, *aIt,
                                       bitMapAllocator);
        }
        if (bBounds.first < aBounds.first) {
          result.definitions[i].insert({bBounds.first, aBounds.first}, *bIt,
                                       bitMapAllocator);
        }
        if (aBounds.second > bBounds.second) {
          result.definitions[i].insert({bBounds.second, aBounds.second}, *aIt,
                                       bitMapAllocator);
        }
        if (bBounds.second > aBounds.second) {
          result.definitions[i].insert({aBounds.second, bBounds.second}, *bIt,
                                       bitMapAllocator);
        }
        auto &node = graph.addNode(std::make_unique<Merge>());
        if (*aIt) {
          auto &edgea = graph.addEdge(**aIt, node);
          edgea.setVariable(slotToSymbol[i], aBounds);
        }
        if (*bIt) {
          auto &edgeb = graph.addEdge(**bIt, node);
          edgeb.setVariable(slotToSymbol[i], bBounds);
        }
        result.definitions[i].insert({std::max(aBounds.first, bBounds.first),
                                      std::min(aBounds.second, bBounds.second)},
                                     &node, bitMapAllocator);
        if (aBounds.second < bBounds.second) {
          ++aIt;
        } else {
          ++bIt;
        }
      } else {
        if (aBounds.second < bBounds.second) {
          result.definitions[i].insert(aBounds, *aIt, bitMapAllocator);
          ++aIt;
        } else {
          result.definitions[i].insert(bBounds, *bIt, bitMapAllocator);
          ++bIt;
        }
      }
    }
    while (aIt != a.definitions[i].end()) {
      result.definitions[i].insert(aIt.bounds(), *aIt, bitMapAllocator);
      ++aIt;
    }
    while (bIt != b.definitions[i].end()) {
      result.definitions[i].insert(bIt.bounds(), *bIt, bitMapAllocator);
      ++bIt;
    }
  }
  auto mergeNodes = [&](NetlistNode *a, NetlistNode *b) -> NetlistNode * {
    if (a && b) {
      if (a != b) {
        auto &node = graph.addNode(std::make_unique<Merge>());
        graph.addEdge(*a, node);
        graph.addEdge(*b, node);
        return &node;
      }
      return a;
    } else if (a && b == nullptr) {
      return a;
    } else if (b && a == nullptr) {
      return b;
    } else {
      return nullptr;
    }
  };
  result.node = mergeNodes(a.node, b.node);
  result.condition = mergeNodes(a.condition, b.condition);
  result.reachable = a.reachable;
  DEBUG_PRINT(
      "Merged states: a.defs.size={}, b.defs.size={}, result.defs.size={}\n",
      a.definitions.size(), b.definitions.size(), result.definitions.size());
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
  for (const auto &i : source.definitions) {
    result.definitions.emplace_back(i.clone(bitMapAllocator));
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
