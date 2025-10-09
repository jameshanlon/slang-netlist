#pragma once

#include "netlist/Debug.hpp"
#include "netlist/IntervalMapUtils.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/SymbolTracker.hpp"

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/util/BumpAllocator.h"
#include "slang/util/IntervalMap.h"

namespace slang::netlist {

struct AnalysisState {

  // Each tracked variable has its definitions intervals stored here.
  SymbolDrivers symbolDrivers;

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

struct PendingLvalue {
  not_null<const ast::ValueSymbol *> symbol;
  const ast::Expression *lsp;
  DriverBitRange bounds;
  NetlistNode *node{nullptr};

  PendingLvalue(const ast::ValueSymbol *symbol, const ast::Expression *lsp,
                DriverBitRange bounds, NetlistNode *node)
      : symbol(symbol), lsp(lsp), bounds(bounds), node(node) {}
};

/// A data flow analysis used as part of the netlist graph construction.
struct DataFlowAnalysis
    : public analysis::AbstractFlowAnalysis<DataFlowAnalysis, AnalysisState> {

  using ParentAnalysis =
      analysis::AbstractFlowAnalysis<DataFlowAnalysis, AnalysisState>;

  friend class AbstractFlowAnalysis;

  template <typename TOwner> friend struct ast::LSPVisitor;

  analysis::AnalysisManager &analysisManager;

  // Symbol to bit ranges mapping to the netlist node(s) that are driving them.
  SymbolTracker symbolTracker;

  // The currently active longest static prefix expression, if there is one.
  ast::LSPVisitor<DataFlowAnalysis> lspVisitor;

  // Track attributes of the current assignment expression.
  bool isLValue = false;
  bool isBlocking = false;
  bool prohibitLValue = false;

  // A reference to the netlist graph under construction.
  NetlistGraph &graph;

  // An external node that is used as a root for the the DFA. For example, a
  // port node that is created by the DFA caller to reference port connection
  // lvalues against.
  NetlistNode *externalNode;

  // Pending L-values from non-blocking assignments that need to be processed at
  // the end of the procedural block.
  std::vector<PendingRvalue> pendingLValues;

  DataFlowAnalysis(analysis::AnalysisManager &analysisManager,
                   ast::Symbol const &symbol, NetlistGraph &graph,
                   NetlistNode *externalNode = nullptr)
      : AbstractFlowAnalysis(symbol, {}), analysisManager(analysisManager),
        lspVisitor(*this), graph(graph), externalNode(externalNode) {}

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

  //===---------------------------------------------------------===//
  // L- and R-value handling.
  //===---------------------------------------------------------===//

  void handleRvalue(ast::ValueSymbol const &symbol, ast::Expression const &lsp,
                    DriverBitRange bounds);

  void handleLvalue(ast::ValueSymbol const &symbol, ast::Expression const &lsp,
                    DriverBitRange bounds);

  /// As per DataFlowAnalysis in upstream slang, but with custom handling of
  /// L- and R-values. Called by the LSP visitor.
  void noteReference(ast::ValueSymbol const &symbol,
                     ast::Expression const &lsp);

  /// Finalize the analysis by processing any pending non-blocking L-values.
  /// This should be called after the main analysis has completed.
  void finalize();

  //===---------------------------------------------------------===//
  // AST handlers
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

  void updateNode(NetlistNode *node, bool conditional);

  void handle(const ast::ProceduralAssignStatement &stmt);

  void handle(const ast::AssignmentExpression &expr);

  void handle(ast::ConditionalStatement const &stmt);

  void handle(ast::CaseStatement const &stmt);

  //===---------------------------------------------------------===//
  // State management
  //===---------------------------------------------------------===//

  AnalysisState mergeStates(AnalysisState const &a, AnalysisState const &b);

  void joinState(AnalysisState &result, AnalysisState const &other);

  void meetState(AnalysisState &result, AnalysisState const &other);

  AnalysisState copyState(AnalysisState const &source);

  static AnalysisState unreachableState();
  static AnalysisState topState();

private:
  void addNonBlockingLvalue(ast::ValueSymbol const &symbol,
                            ast::Expression const &lsp, DriverBitRange bounds,
                            NetlistNode *node);

  void processNonBlockingLvalues();
};

} // namespace slang::netlist
