#pragma once

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/util/BumpAllocator.h"
#include "slang/util/IntervalMap.h"

#include "netlist/Debug.hpp"
#include "netlist/NetlistGraph.hpp"

namespace slang::netlist {

// Map definitions ranges to graph nodes.
using SymbolLSPMap = IntervalMap<uint64_t, const ast::Expression *, 8>;

struct SLANG_EXPORT AnalysisState {

  // Each tracked variable has its definitions intervals stored here.
  std::vector<SymbolDriverMap> definitions;

  // The current control flow node in the graph.
  NetlistNode *node{nullptr};

  // The previous branching condition node in the graph.
  NetlistNode *condition{nullptr};

  // Whether the control flow that arrived at this point is reachable.
  bool reachable = true;

  AnalysisState() = default;
  AnalysisState(AnalysisState &&other) = default;
  auto operator=(AnalysisState &&other) -> AnalysisState & = default;
};

struct ProceduralAnalysis
    : public analysis::AbstractFlowAnalysis<ProceduralAnalysis, AnalysisState> {

  using ParentAnalysis =
      analysis::AbstractFlowAnalysis<ProceduralAnalysis, AnalysisState>;

  friend class AbstractFlowAnalysis;

  template <typename TOwner> friend struct ast::LSPVisitor;

  analysis::AnalysisManager &analysisManager;

  BumpAllocator allocator;
  SymbolDriverMap::allocator_type bitMapAllocator;
  SymbolLSPMap::allocator_type lspMapAllocator;

  // Maps visited symbols to slots in definitions vectors.
  SymbolSlotMap symbolToSlot;

  // The currently active longest static prefix expression, if there is one.
  ast::LSPVisitor<ProceduralAnalysis> lspVisitor;
  bool isLValue = false;
  bool prohibitLValue = false;

  // A reference to the netlist graph under construction.
  NetlistGraph &graph;

  ProceduralAnalysis(analysis::AnalysisManager &analysisManager,
                     const ast::Symbol &symbol, NetlistGraph &graph)
      : AbstractFlowAnalysis(symbol, {}), analysisManager(analysisManager),
        bitMapAllocator(allocator), lspMapAllocator(allocator),
        lspVisitor(*this), graph(graph) {}

  auto getState() -> AnalysisState & { return ParentAnalysis::getState(); }
  auto getState() const -> AnalysisState const & {
    return ParentAnalysis::getState();
  }

  [[nodiscard]] auto saveLValueFlag() {
    auto guard =
        ScopeGuard([this, savedLVal = isLValue] { isLValue = savedLVal; });
    isLValue = false;
    return guard;
  }

  /// Lookup a ValueDriver for the given symbol and bounds.
  /// Returns std::nullopt if no driver is found.
  [[nodiscard]] auto getDriver(const ast::ValueSymbol &symbol,
                               std::pair<uint32_t, uint32_t> bounds)
      -> analysis::ValueDriver const * {
    // Get the driver for the symbol at the given bounds.
    auto drivers = analysisManager.getDrivers(symbol);
    for (auto [driver, bitRange] : drivers) {
      if (ConstantRange(bitRange).contains(ConstantRange(bounds))) {
        return driver;
      }
    }
    // No driver found for the symbol at the given bounds.
    return nullptr;
  }

  void handleRvalue(const ast::ValueSymbol &symbol,
                    std::pair<uint32_t, uint32_t> bounds) {
    DEBUG_PRINT("Handle R-value: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);

    if (symbolToSlot.contains(&symbol)) {
      // Symbol is assigned in this procedural block.

      auto &currState = getState();
      auto index = symbolToSlot.at(&symbol);
      auto &definitions = currState.definitions[index];

      for (auto it = definitions.find(bounds); it != definitions.end(); it++) {

        auto itBounds = it.bounds();
        auto &currState = getState();

        // R-value bounds completely contains a definition bounds.
        if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {

          // Add an edge from the definition node to the current node using it.
          if (currState.node) {
            auto &edge = graph.addEdge(**it, *currState.node);
            edge.setVariable(&symbol, bounds);
          }
        }

        // R-value bounds completely contain a definition bounds.
        if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {

          // Add an edge from the definition node to the current node using it.
          SLANG_ASSERT(currState.node);
          auto &edge = graph.addEdge(**it, *currState.node);
          edge.setVariable(&symbol, bounds);
        }
      }

    } else {
      // Otherwise, the symbol is assigned outside of this procedural block.
      // TODO
    }
  }

  auto handleLvalue(const ast::ValueSymbol &symbol, const ast::Expression &lsp,
                    std::pair<uint32_t, uint32_t> bounds) {

    DEBUG_PRINT("Handle L-value: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);

    auto &currState = getState();

    // Update visited symbols to slots.
    auto [it, inserted] =
        symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());

    // Update current state definitions.
    auto index = it->second;
    if (index >= currState.definitions.size()) {
      currState.definitions.resize(index + 1);
    }

    auto &definitions = currState.definitions[index];
    for (auto it = definitions.find(bounds); it != definitions.end();) {
      auto itBounds = it.bounds();

      // Existing entry completely contains new bounds, so split entry.
      if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {
        definitions.erase(it, bitMapAllocator);
        definitions.insert({itBounds.first, bounds.first}, *it,
                           bitMapAllocator);
        definitions.insert({bounds.second, itBounds.second}, *it,
                           bitMapAllocator);
        break;
      }

      // New bounds completely contain an existing entry, so delete entry.
      if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {
        definitions.erase(it, bitMapAllocator);
        it = definitions.find(bounds);
      } else {
        ++it;
      }
    }

    // Insert the new definition.
    definitions.insert(bounds, currState.node, bitMapAllocator);
  }

  /// As per DataFlowAnalysis in upstream slang, but with custom handling of L-
  /// and R-values. Called by the LSP visitor.
  void noteReference(const ast::ValueSymbol &symbol,
                     const ast::Expression &lsp) {

    // This feels icky but we don't count a symbol as being referenced in the
    // procedure if it's only used inside an unreachable flow path. The
    // alternative would just frustrate users, but the reason it's icky is
    // because whether a path is reachable is based on whatever level of
    // heuristics we're willing to implement rather than some well defined set
    // of rules in the LRM.
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
      handleRvalue(symbol, *bounds);
    }
  }

  //===---------------------------------------------------------===//
  // AST Handlers
  //===---------------------------------------------------------===//

  template <typename T>
  requires(std::is_base_of_v<ast::Expression, T> && !ast::IsSelectExpr<T>)
  void handle(const T &expr) {
    lspVisitor.clear();
    visitExpr(expr);
  }

  template <typename T>
  requires(ast::IsSelectExpr<T>)
  void handle(const T &expr) {
    lspVisitor.handle(expr);
  }

  void updateNode(NetlistNode *node, bool conditional) {
    auto &currState = getState();

    // If there is a previoius conditional node, then add an edge
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

  void handle(const ast::ProceduralAssignStatement &stmt) {
    // Procedural force statements don't act as drivers of their lvalue target.
    if (stmt.isForce) {
      prohibitLValue = true;
      visitStmt(stmt);
      prohibitLValue = false;
    } else {
      visitStmt(stmt);
    }
  }

  void handle(const ast::AssignmentExpression &expr) {
    DEBUG_PRINT("AssignmentExpression\n");

    auto &node = graph.addNode(std::make_unique<Assignment>());

    updateNode(&node, false);

    // Note that this method mirrors the logic in the base class
    // handler but we need to track the LValue status of the lhs.
    if (!prohibitLValue) {
      SLANG_ASSERT(!isLValue);
      isLValue = true;
      visit(expr.left());
      isLValue = false;
    } else {
      visit(expr.left());
    }

    if (!expr.isLValueArg()) {
      visit(expr.right());
    }
  }

  void handle(const ast::ConditionalStatement &stmt) {
    DEBUG_PRINT("ConditionalStatement\n");

    // If all conditons are constant, then there is no need to include this as a
    // node.
    if (std::all_of(stmt.conditions.begin(), stmt.conditions.end(),
                    [&](ast::ConditionalStatement::Condition const &cond) {
                      return tryEvalBool(*cond.expr);
                    })) {
      visitStmt(stmt);
      return;
    }

    auto &node = graph.addNode(std::make_unique<Conditional>());

    updateNode(&node, true);

    visitStmt(stmt);
  }

  void handle(ast::CaseStatement const &stmt) {
    DEBUG_PRINT("CaseStatement\n");

    auto &node = graph.addNode(std::make_unique<Case>());

    updateNode(&node, true);

    visitStmt(stmt);
  }

  //===---------------------------------------------------------===//
  // State Management
  //===---------------------------------------------------------===//

  void mergeStates(AnalysisState &result, AnalysisState const &other) {
    // Resize result.
    if (result.definitions.size() < other.definitions.size()) {
      result.definitions.resize(other.definitions.size());
    }
    // For each symbol, insert intervals from other into result.
    for (size_t i = 0; i < other.definitions.size(); i++) {
      for (auto it = other.definitions[i].begin();
           it != other.definitions[i].end(); ++it) {
        result.definitions[i].insert(it.bounds(), *it, bitMapAllocator);
        // TODO: for overlapping intervals: split off the non-overlapping
        // parts, create a node for each overlapping region and add edges
        // from each range in that region to the node.
      }
    }
  }

  void joinState(AnalysisState &result, const AnalysisState &other) {
    DEBUG_PRINT("joinState\n");
    if (result.reachable == other.reachable) {
      mergeStates(result, other);
    } else if (!result.reachable) {
      result = copyState(other);
    }
  }

  void meetState(AnalysisState &result, const AnalysisState &other) {
    DEBUG_PRINT("meetState\n");
    if (!other.reachable) {
      result.reachable = false;
      return;
    }
    mergeStates(result, other);
  }

  auto copyState(const AnalysisState &source) -> AnalysisState {
    DEBUG_PRINT("copyState\n");
    AnalysisState result;
    result.reachable = source.reachable;
    result.definitions.reserve(source.definitions.size());
    for (const auto &i : source.definitions) {
      result.definitions.emplace_back(i.clone(bitMapAllocator));
    }
    result.node = source.node;
    result.condition = source.condition;
    return result;
  }

  static auto unreachableState() -> AnalysisState {
    DEBUG_PRINT("unreachableState\n");
    AnalysisState result;
    result.reachable = false;
    return result;
  }

  static auto topState() -> AnalysisState { return {}; }
};

} // namespace slang::netlist
