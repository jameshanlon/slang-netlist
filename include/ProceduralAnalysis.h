#pragma once

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/ast/statements/ConditionalStatements.h"
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
using SymbolBitMap = IntervalMap<uint64_t, NetlistNode *, 3>;
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

  /// The current control flow node in the graph.
  NetlistNode *node{nullptr};

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
 
  /// Find the LSP for the symbol with the given index and bounds. 
  const ast::Expression *findLsp(uint32_t index, std::pair<uint64_t, uint64_t> bounds) {
    auto &lspMap = lvalues[index].assigned;
    for (auto lspIt = lspMap.find(bounds); lspIt != lspMap.end(); lspIt++) {
      if (ConstantRange(lspIt.bounds()) == ConstantRange(bounds)) {
        return *lspIt;
      }
    } 
    // Shouldn't get here.
    SLANG_UNREACHABLE;
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
      
      // Create a variable node.
      auto &node = graph.addNode(std::make_unique<VariableReference>(symbol, lsp));

      // Add an edge from current state to the variable.
      auto &currState = getState();
      if (currState.node) {
        graph.addEdge(*currState.node, node);
      }

      // Update visited symbols to slots.
      auto [it, inserted] =
          symbolToSlot.try_emplace(&symbol, (uint32_t)lvalues.size());

      // Update assigned ranges of L-values.
      if (inserted) {
        lvalues.emplace_back(symbol);
        SLANG_ASSERT(lvalues.size() == symbolToSlot.size());
      }

      // Update current state assigned.
      auto index = it->second;
      if (index >= currState.assigned.size()) {
        currState.assigned.resize(index + 1);
      }

      //currState.assigned[index].unionWith(*bounds, {}, bitMapAllocator);
      auto &assigned = currState.assigned[index];
      for (auto assIt = assigned.find(*bounds); assIt != assigned.end();) {

        auto itBounds = assIt.bounds();

        // Existing entry completely contains new bounds.
        if (ConstantRange(itBounds).contains(ConstantRange(*bounds))) {
          // Split entry.
          assigned.erase(assIt, bitMapAllocator);
          assigned.insert({itBounds.first, bounds->first}, *assIt, bitMapAllocator);
          assigned.insert({bounds->second, itBounds.second}, *assIt, bitMapAllocator);
          break;
        }

        // New bounds completely contain an existing entry.
        if (ConstantRange(*bounds).contains(ConstantRange(itBounds))) {
          // Delete entry.
          assigned.erase(assIt, bitMapAllocator);
          assIt = assigned.find(*bounds);
        } else {
          ++assIt;
        }
      }
      assigned.insert(*bounds, &node, bitMapAllocator);

      // Update LSP map.
      auto &lspMap = lvalues[index].assigned;
      for (auto lspIt = lspMap.find(*bounds); lspIt != lspMap.end();) {

        // If we find an existing entry that completely contains
        // the new bounds we can just keep that one and ignore the
        // new one. Otherwise we will insert a new entry.
        auto itBounds = lspIt.bounds();
        if (ConstantRange(itBounds).contains(ConstantRange(*bounds))) {
          return;
        }

        // If the new bounds completely contain the existing entry, we can
        // remove it.
        if (ConstantRange(*bounds).contains(ConstantRange(itBounds))) {
          lspMap.erase(lspIt, lspMapAllocator);
          lspIt = lspMap.find(*bounds);
        } else {
          ++lspIt;
        }
      }
      lspMap.insert(*bounds, &lsp, lspMapAllocator);

    } else {
      rvalues[&symbol].unionWith(*bounds, {}, bitMapAllocator);

      if (!symbolToSlot.contains(&symbol)) {
        // Symbol not assigned in this procedural block.
        return;
      }

      auto index = symbolToSlot.at(&symbol);
      auto &assigned = currState.assigned[index];

      for (auto it = assigned.find(*bounds); it != assigned.end();) {
        auto itBounds = it.bounds();

        // Existing entry completely contains new bounds.
        if (ConstantRange(itBounds).contains(ConstantRange(*bounds))) {
      
          // Add an edge from the variable to the current state node.
          auto &currState = getState();
          SLANG_ASSERT(currState.node);
          graph.addEdge(**it, *currState.node);
          return;
        }

        // New bounds completely contain an existing entry.
        if (ConstantRange(*bounds).contains(ConstantRange(itBounds))) {
          
          // Add an edge from the variable to the current state node.
          auto &currState = getState();
          SLANG_ASSERT(currState.node);
          graph.addEdge(**it, *currState.node);
        }
      }
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

    auto &currState = getState();
    auto &node = graph.addNode(std::make_unique<NetlistNode>(NodeKind::Assignment));
    
    if (currState.node) {
      graph.addEdge(*currState.node, node);
    }

    currState.node = &node;

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
    
    auto &currState = getState();
    auto &node = graph.addNode(std::make_unique<NetlistNode>(NodeKind::Conditional));
   
    if (currState.node) {
      graph.addEdge(*currState.node, node);
    }

    currState.node = &node;

    visitStmt(stmt);
  }

  void handle(ast::CaseStatement const &stmt) {
    fmt::print("CaseStatement\n");

    auto &currState = getState();
    auto &node = graph.addNode(std::make_unique<NetlistNode>(NodeKind::Case));
   
    if (currState.node) {
      graph.addEdge(*currState.node, node);
    }

    currState.node = &node;

    visitStmt(stmt);
  }

  // **** State Management ****

  void joinState(AnalysisState &result, const AnalysisState &other) {
    fmt::print("joinState\n");
    if (result.reachable == other.reachable) {

      // Intersect assigned.
      if (result.assigned.size() > other.assigned.size()) {
        result.assigned.resize(other.assigned.size());
      }

      for (size_t i = 0; i < result.assigned.size(); i++) {

        // Determine intersecting assignments.
        auto updated =
            result.assigned[i].intersection(other.assigned[i], bitMapAllocator);

        // For each interval in the intersection, and a node and any edges to
        // that node.
        for (auto updatedIt = updated.begin(); updatedIt != updated.end(); updatedIt++) {

            // Create a new node for each interval in updated.
            auto* lsp = findLsp(i, updatedIt.bounds());
            auto& node = graph.addNode(
                std::make_unique<VariableReference>(*lvalues[i].symbol.get(), *lsp));

            // Attach the node to the new interval.
            *updatedIt = &node;

            // For each interval in 'result' add out edges.
            for (auto resultIt = result.assigned[i].find(updatedIt.bounds());
                 resultIt != result.assigned[i].end(); resultIt++) {
              graph.addEdge(*const_cast<NetlistNode*>(*resultIt), node);
            }
            
            // For each interval in 'other' add out edges.
            for (auto otherIt = other.assigned[i].find(updatedIt.bounds());
                 otherIt != other.assigned[i].end(); otherIt++) {
              graph.addEdge(*const_cast<NetlistNode*>(*otherIt), node);
            }
        }

        result.assigned[i] = std::move(updated);
      }

      //// Create a join node.
      //auto &node = graph.addNode(std::make_unique<Join>());
      //result.node = &node;

      //if (other.node) {
      //  graph.addEdge(*other.node, node);
      //}

    } else if (!result.reachable) {
      result = copyState(other);
    }
  }

  void meetState(AnalysisState &result, const AnalysisState &other) {
    fmt::print("meetState\n");
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
      
    // Create a join node.
    auto &node = graph.addNode(std::make_unique<Join>());
    result.node = &node;

    if (other.node) {
      graph.addEdge(*other.node, node);
    }
  }

  AnalysisState copyState(const AnalysisState &source) {
    fmt::print("copyState\n");
    AnalysisState result;
    result.reachable = source.reachable;
    result.assigned.reserve(source.assigned.size());
    for (size_t i = 0; i < source.assigned.size(); i++) {
      result.assigned.emplace_back(source.assigned[i].clone(bitMapAllocator));
    }
    result.node = source.node;
    // Create a new node here?
    return result;
  }

  AnalysisState unreachableState() const {
    fmt::print("unreachableState\n");
    AnalysisState result;
    result.reachable = false;
    return result;
  }

  AnalysisState topState() const { return {}; }
};

} // namespace slang::netlist
