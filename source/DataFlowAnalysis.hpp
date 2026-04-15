#pragma once

#include "DriverMap.hpp"
#include "IntervalMapUtils.hpp"
#include "PendingRValue.hpp"
#include "ValueTracker.hpp"

#include "netlist/Debug.hpp"

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/DataFlowAnalysis.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/symbols/ClassSymbols.h"
#include "slang/util/BumpAllocator.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/ScopeGuard.h"

namespace slang::netlist {

class NetlistBuilder;

struct AnalysisState {

  // Each tracked variable has its definitions intervals stored here.
  ValueDrivers valueDrivers;

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

  analysis::AnalysisManager &analysisManager;

  // ValueSymbol to bit ranges mapping to the netlist node(s) that are driving
  // them.
  ValueTracker valueTracker;

  // Track attributes of the current assignment expression.
  bool isLValue = false;
  bool isBlocking = false;
  bool prohibitLValue = false;

  // A reference to the netlist graph under construction.
  NetlistBuilder &builder;

  // An external node that is used as a root for the the DFA. For example, a
  // port node that is created by the DFA caller to reference port connection
  // lvalues against.
  NetlistNode *externalNode;

  // Pending L-values from non-blocking assignments that need to be processed at
  // the end of the procedural block.
  std::vector<PendingRvalue> pendingLValues;

  // Allocator reused across every handleRvalue() invocation within the DFA.
  BumpAllocator rvalueAllocator;
  DriverMap::AllocatorType rvalueMapAllocator{rvalueAllocator};

  DataFlowAnalysis(analysis::AnalysisManager &analysisManager,
                   ast::Symbol const &symbol, NetlistBuilder &builder,
                   NetlistNode *externalNode = nullptr)
      : AbstractFlowAnalysis(symbol, {}), analysisManager(analysisManager),
        builder(builder), externalNode(externalNode) {}

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
  /// L- and R-values.
  void noteReference(ast::ValuePath const &path);

  /// Finalize the analysis by processing any pending non-blocking L-values.
  /// This should be called after the main analysis has completed.
  void finalize();

  //===---------------------------------------------------------===//
  // AST handlers
  //===---------------------------------------------------------===//

  template <typename T>
    requires(analysis::detail::IsSelectExpr<T>)
  void handle(const T &expr) {
    auto clearLValFlag = [this]() {
      auto guard =
          ScopeGuard([this, savedLVal = isLValue] { isLValue = savedLVal; });
      isLValue = false;
      return guard;
    };

    ast::ValuePath path(expr, this->getEvalContext());
    for (auto &elem : path) {
      switch (elem.kind) {
      case ast::ExpressionKind::NamedValue:
      case ast::ExpressionKind::HierarchicalValue:
        break;
      case ast::ExpressionKind::ElementSelect: {
        auto guard = clearLValFlag();
        this->visit(elem.as<ast::ElementSelectExpression>().selector());
        break;
      }
      case ast::ExpressionKind::RangeSelect: {
        auto guard = clearLValFlag();
        auto &rs = elem.as<ast::RangeSelectExpression>();
        this->visit(rs.left());
        this->visit(rs.right());
        break;
      }
      case ast::ExpressionKind::MemberAccess: {
        auto &mae = elem.as<ast::MemberAccessExpression>();
        if (auto prop = mae.member.as_if<ast::ClassPropertySymbol>();
            prop && prop->lifetime == ast::VariableLifetime::Static) {
          auto guard = clearLValFlag();
          this->visit(mae.value());
        }
        break;
      }
      default: {
        auto guard = clearLValFlag();
        this->visit(elem);
        break;
      }
      }
    }

    noteReference(path);
  }

  void updateNode(NetlistNode *node, bool conditional);

  void handle(const ast::ProceduralAssignStatement &stmt);

  void handle(const ast::AssignmentExpression &expr);

  void handle(ast::ConditionalStatement const &stmt);

  void handle(ast::CaseStatement const &stmt);

  //===---------------------------------------------------------===//
  // State management
  //===---------------------------------------------------------===//

  auto mergeStates(AnalysisState &result, AnalysisState const &other);

  void joinState(AnalysisState &result, AnalysisState const &other);

  void meetState(AnalysisState &result, AnalysisState const &other);

  auto copyState(AnalysisState const &source) -> AnalysisState;

  static auto unreachableState() -> AnalysisState;
  static auto topState() -> AnalysisState;

private:
  void addNonBlockingLvalue(ast::ValueSymbol const &symbol,
                            ast::Expression const &lsp, DriverBitRange bounds,
                            NetlistNode *node);

  void processNonBlockingLvalues();
};

} // namespace slang::netlist
