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
using SymbolLSPMap = IntervalMap<uint64_t, const ast::Expression *, 5>;

struct SLANG_EXPORT AnalysisState {

  // Whether the control flow that arrived at this point is reachable.
  bool reachable = true;

  // The current control flow node in the graph.
  NetlistNode *node{nullptr};

  // The previous branching condition node in the graph.
  NetlistNode *condition{nullptr};

  AnalysisState() = default;
  AnalysisState(AnalysisState &&other) = default;
  auto operator=(AnalysisState &&other) -> AnalysisState & = default;
};

struct ProceduralAnalysis
    : public analysis::AbstractFlowAnalysis<ProceduralAnalysis, AnalysisState> {

  friend class AbstractFlowAnalysis;

  template <typename TOwner> friend struct ast::LSPVisitor;

  analysis::AnalysisManager &analysisManager;

  BumpAllocator allocator;
  SymbolLSPMap::allocator_type lspMapAllocator;

  // The currently active longest static prefix expression, if there is one.
  ast::LSPVisitor<ProceduralAnalysis> lspVisitor;
  bool isLValue = false;
  bool prohibitLValue = false;

  // A reference to the netlist graph under construction.
  NetlistGraph &graph;

  ProceduralAnalysis(analysis::AnalysisManager &analysisManager,
                     const ast::Symbol &symbol, NetlistGraph &graph)
      : AbstractFlowAnalysis(symbol, {}), analysisManager(analysisManager),
        lspMapAllocator(allocator), lspVisitor(*this), graph(graph) {}

  [[nodiscard]] auto saveLValueFlag() {
    auto guard =
        ScopeGuard([this, savedLVal = isLValue] { isLValue = savedLVal; });
    isLValue = false;
    return guard;
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
      graph.handleLvalue(symbol, lsp, *bounds, currState.node);
    } else {
      graph.handleRvalue(symbol, *bounds, currState.node);
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
    //// Resize result.
    // if (result.definitions.size() < other.definitions.size()) {
    //   result.definitions.resize(other.definitions.size());
    // }
    //// For each symbol, insert intervals from other into result.
    // for (size_t i = 0; i < other.definitions.size(); i++) {
    //   for (auto it = other.definitions[i].begin();
    //        it != other.definitions[i].end(); ++it) {
    //     result.definitions[i].insert(it.bounds(), *it, bitMapAllocator);
    //   }
    // }
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
    // result.definitions.reserve(source.definitions.size());
    // for (const auto &i : source.definitions) {
    //   result.definitions.emplace_back(i.clone(bitMapAllocator));
    // }
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
