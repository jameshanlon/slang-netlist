#pragma once

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/util/BumpAllocator.h"
#include "slang/util/IntervalMap.h"
#include "slang/text/FormatBuffer.h"

#include "NetlistGraph.h"

namespace slang::netlist {

template <typename T>
concept IsSelectExpr =
    IsAnyOf<T, ast::ElementSelectExpression, ast::RangeSelectExpression,
            slang::ast::MemberAccessExpression,
            ast::HierarchicalValueExpression, ast::NamedValueExpression>;

// Map assigned ranges to graph nodes.
using SymbolBitMap = IntervalMap<uint64_t, std::monostate, 3>;
using SymbolLSPMap = IntervalMap<uint64_t, const ast::Expression *, 5>;

/// Convert a LSP expression into a string for reporting.
/// (Copied from AnalyzedProcedure.cpp)
static void stringifyLSP(const ast::Expression &expr,
                         ast::EvalContext &evalContext, FormatBuffer &buffer) {
  switch (expr.kind) {
  case ast::ExpressionKind::NamedValue:
  case ast::ExpressionKind::HierarchicalValue:
    buffer.append(expr.as<ast::ValueExpressionBase>().symbol.name);
    break;
  case ast::ExpressionKind::Conversion:
    stringifyLSP(expr.as<ast::ConversionExpression>().operand(), evalContext,
                 buffer);
    break;
  case ast::ExpressionKind::ElementSelect: {
    auto &select = expr.as<ast::ElementSelectExpression>();
    stringifyLSP(select.value(), evalContext, buffer);
    buffer.format("[{}]", select.selector().eval(evalContext).toString());
    break;
  }
  case ast::ExpressionKind::RangeSelect: {
    auto &select = expr.as<ast::RangeSelectExpression>();
    stringifyLSP(select.value(), evalContext, buffer);
    buffer.format("[{}:{}]", select.left().eval(evalContext).toString(),
                  select.right().eval(evalContext).toString());
    break;
  }
  case ast::ExpressionKind::MemberAccess: {
    auto &access = expr.as<ast::MemberAccessExpression>();
    stringifyLSP(access.value(), evalContext, buffer);
    buffer.append(".");
    buffer.append(access.member.name);
    break;
  }
  default:
    SLANG_UNREACHABLE;
  }
}

/// A helper class that finds the longest static prefix of select expressions.
/// (Copied from DataFlowAnalysis.h)
template <typename TOwner> struct LSPVisitor {
  TOwner &owner;
  const ast::Expression *currentLSP = nullptr;

  explicit LSPVisitor(TOwner &owner) : owner(owner) {}

  void clear() { currentLSP = nullptr; }

  void handle(const ast::ElementSelectExpression &expr) {
    if (expr.isConstantSelect(owner.getEvalContext())) {
      if (!currentLSP) {
        currentLSP = &expr;
      }
    } else {
      currentLSP = nullptr;
    }

    owner.visit(expr.value());

    [[maybe_unused]] auto guard = owner.saveLValueFlag();
    owner.visit(expr.selector());
  }

  void handle(const ast::RangeSelectExpression &expr) {
    if (expr.isConstantSelect(owner.getEvalContext())) {
      if (!currentLSP) {
        currentLSP = &expr;
      }
    } else {
      currentLSP = nullptr;
    }

    owner.visit(expr.value());

    [[maybe_unused]] auto guard = owner.saveLValueFlag();
    owner.visit(expr.left());
    owner.visit(expr.right());
  }

  void handle(const ast::MemberAccessExpression &expr) {
    // If this is a selection of a class or covergroup member,
    // the lsp depends only on the selected member and not on
    // the handle itself. Otherwise, the opposite is true.
    auto &valueType = expr.value().type->getCanonicalType();
    if (valueType.isClass() || valueType.isCovergroup() || valueType.isVoid()) {
      auto lsp = std::exchange(currentLSP, nullptr);
      if (!lsp) {
        lsp = &expr;
      }

      if (ast::VariableSymbol::isKind(expr.member.kind))
        owner.noteReference(expr.member.as<ast::VariableSymbol>(), *lsp);

      // Make sure the value gets visited but not as an lvalue anymore.
      [[maybe_unused]] auto guard = owner.saveLValueFlag();
      owner.visit(expr.value());
    } else {
      if (!currentLSP) {
        currentLSP = &expr;
      }

      owner.visit(expr.value());
    }
  }

  void handle(const ast::HierarchicalValueExpression &expr) {
    auto lsp = std::exchange(currentLSP, nullptr);
    if (!lsp) {
      lsp = &expr;
    }

    owner.noteReference(expr.symbol, *lsp);
  }

  void handle(const ast::NamedValueExpression &expr) {
    auto lsp = std::exchange(currentLSP, nullptr);
    if (!lsp) {
      lsp = &expr;
    }

    owner.noteReference(expr.symbol, *lsp);
  }
};

struct SLANG_EXPORT AnalysisState {

  /// Each tracked variable has its assigned intervals stored here.
  SmallVector<SymbolBitMap, 2> assigned;

  /// Whether the control flow that arrived at this point is reachable.
  bool reachable = true;

  AnalysisState() = default;
  AnalysisState(AnalysisState &&other) = default;
  AnalysisState &operator=(AnalysisState &&other) = default;
};

struct ProceduralAnalysis
    : public analysis::AbstractFlowAnalysis<ProceduralAnalysis, AnalysisState> {

  friend class AbstractFlowAnalysis;

  template <typename TOwner> friend struct LSPVisitor;

  BumpAllocator allocator;
  SymbolBitMap::allocator_type bitMapAllocator;
  SymbolLSPMap::allocator_type lspMapAllocator;

  // Maps visited symbols to slots in assigned vectors.
  SmallMap<const ast::ValueSymbol *, uint32_t, 4> symbolToSlot;

  // Tracks the assigned ranges of each variable across the entire procedure,
  // even if not all branches assign to it.
  struct LValueSymbol {
    not_null<const ast::ValueSymbol *> symbol;
    SymbolLSPMap assigned;

    LValueSymbol(const ast::ValueSymbol &symbol) : symbol(&symbol) {}
  };
  SmallVector<LValueSymbol> lvalues;

  // All of the nets and variables that have been read in the procedure.
  SmallMap<const ast::ValueSymbol *, SymbolBitMap, 4> rvalues;

  // The currently active longest static prefix expression, if there is one.
  LSPVisitor<ProceduralAnalysis> lspVisitor;
  bool isLValue = false;
  
  // A reference to the netlist graph under construction.
  NetlistGraph &graph;

  ProceduralAnalysis(const ast::Symbol &symbol, NetlistGraph &graph)
      : AbstractFlowAnalysis(symbol, {}), bitMapAllocator(allocator),
        lspMapAllocator(allocator), lspVisitor(*this), graph(graph) {}

  [[nodiscard]] auto saveLValueFlag() {
    auto guard =
        ScopeGuard([this, savedLVal = isLValue] { isLValue = savedLVal; });
    isLValue = false;
    return guard;
  }

  void noteReference(const ast::ValueSymbol &symbol,
                     const ast::Expression &lsp) {
    fmt::print("Note reference: {}\n", symbol.name);

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
        ast::ValueDriver::getBounds(lsp, getEvalContext(), symbol.getType());
    if (!bounds) {
      // This probably cannot be hit given that we early out elsewhere for
      // invalid expressions.
      return;
    }

    if (isLValue) {
      auto [it, inserted] =
          symbolToSlot.try_emplace(&symbol, (uint32_t)lvalues.size());
      if (inserted) {
        lvalues.emplace_back(symbol);
        SLANG_ASSERT(lvalues.size() == symbolToSlot.size());
      }

      auto index = it->second;
      if (index >= currState.assigned.size()) {
        currState.assigned.resize(index + 1);
      }

      currState.assigned[index].unionWith(*bounds, {}, bitMapAllocator);

      auto &lspMap = lvalues[index].assigned;
      for (auto lspIt = lspMap.find(*bounds); lspIt != lspMap.end();) {
        // If we find an existing entry that completely contains
        // the new bounds we can just keep that one and ignore the
        // new one. Otherwise we will insert a new entry.
        auto itBounds = lspIt.bounds();
        if (itBounds.first <= bounds->first &&
            itBounds.second >= bounds->second) {
          return;
        }

        // If the new bounds completely contain the existing entry, we can
        // remove it.
        if (bounds->first < itBounds.first &&
            bounds->second > itBounds.second) {
          lspMap.erase(lspIt, lspMapAllocator);
          lspIt = lspMap.find(*bounds);
        } else {
          ++lspIt;
        }
      }
      lspMap.insert(*bounds, &lsp, lspMapAllocator);
    } else {
      rvalues[&symbol].unionWith(*bounds, {}, bitMapAllocator);
    }
  }

  // **** AST Handlers ****

  template <typename T>
    requires(std::is_base_of_v<ast::Expression, T> && !IsSelectExpr<T>)
  void handle(const T &expr) {
    lspVisitor.clear();
    visitExpr(expr);
  }

  template <typename T>
    requires(IsSelectExpr<T>)
  void handle(const T &expr) {
    lspVisitor.handle(expr);
  }

  void handle(const ast::AssignmentExpression &expr) {
    fmt::print("AssignmentExpression\n");

    graph.addNode(std::make_unique<NetlistNode>(NodeKind::Assignment));

    // Note that this method mirrors the logic in the base class
    // handler but we need to track the LValue status of the lhs.
    SLANG_ASSERT(!isLValue);
    isLValue = true;
    visit(expr.left());
    isLValue = false;

    if (!expr.isLValueArg()) {
      visit(expr.right());
    }
  }

  void handle(const ast::ConditionalStatement &stmt) {
    fmt::print("ConditionalStatement\n");
    
    graph.addNode(std::make_unique<NetlistNode>(NodeKind::Conditional));

    visitStmt(stmt);
  }

  // **** State Management ****

  void joinState(AnalysisState &result, const AnalysisState &other) {
    if (result.reachable == other.reachable) {
      if (result.assigned.size() > other.assigned.size()) {
        result.assigned.resize(other.assigned.size());
      }

      for (size_t i = 0; i < result.assigned.size(); i++) {
        result.assigned[i] =
            result.assigned[i].intersection(other.assigned[i], bitMapAllocator);
      }
    } else if (!result.reachable) {
      result = copyState(other);
    }
  }

  void meetState(AnalysisState &result, const AnalysisState &other) {
    if (!other.reachable) {
      result.reachable = false;
      return;
    }

    // Union the assigned state across each variable.
    if (result.assigned.size() < other.assigned.size()) {
      result.assigned.resize(other.assigned.size());
    }

    for (size_t i = 0; i < other.assigned.size(); i++) {
      for (auto it = other.assigned[i].begin(); it != other.assigned[i].end();
           ++it) {
        result.assigned[i].unionWith(it.bounds(), *it, bitMapAllocator);
      }
    }
  }

  AnalysisState copyState(const AnalysisState &source) {
    AnalysisState result;
    result.reachable = source.reachable;
    result.assigned.reserve(source.assigned.size());
    for (size_t i = 0; i < source.assigned.size(); i++) {
      result.assigned.emplace_back(source.assigned[i].clone(bitMapAllocator));
    }
    return result;
  }

  AnalysisState unreachableState() const {
    AnalysisState result;
    result.reachable = false;
    return result;
  }

  AnalysisState topState() const { return {}; }
};

} // namespace slang::netlist
