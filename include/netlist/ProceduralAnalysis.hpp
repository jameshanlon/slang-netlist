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

// Map assigned ranges to graph nodes.
using SymbolBitMap = IntervalMap<uint64_t, NetlistNode *, 3>;
using SymbolLSPMap = IntervalMap<uint64_t, const ast::Expression *, 5>;

struct SLANG_EXPORT AnalysisState {

  /// Each tracked variable has its assigned intervals stored here.
  SmallVector<SymbolBitMap, 2> assigned;

  /// Whether the control flow that arrived at this point is reachable.
  bool reachable = true;

  /// The current control flow node in the graph.
  NetlistNode *node{nullptr};

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
  SymbolBitMap::allocator_type bitMapAllocator;
  SymbolLSPMap::allocator_type lspMapAllocator;

  // Maps visited symbols to slots in assigned vectors.
  SmallMap<const ast::ValueSymbol *, uint32_t, 4> symbolToSlot;

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

  [[nodiscard]] auto saveLValueFlag() {
    auto guard =
        ScopeGuard([this, savedLVal = isLValue] { isLValue = savedLVal; });
    isLValue = false;
    return guard;
  }

  ///// Lookup a ValueDriver for the given symbol and bounds.
  ///// Returns std::nullopt if no driver is found.
  //[[nodiscard]] auto getDriver(const ast::ValueSymbol &symbol,
  //                             std::pair<uint32_t, uint32_t> bounds)
  //    -> std::optional<analysis::ValueDriver> {
  //  // Get the driver for the symbol at the given bounds.
  //  auto drivers = analysisManager.getDrivers(symbol);
  //  for (auto &[driver, bitRange] : drivers) {
  //    if (ConstantRange(bitRange).contains(ConstantRange(bounds))) {
  //      return *driver;
  //    }
  //  }
  //  // No driver found for the symbol at the given bounds.
  //  return std::nullopt;
  //}

  ///// Find the LSP for the symbol with the given index and bounds.
  // auto findLsp(uint32_t index, std::pair<uint64_t, uint64_t> bounds)
  //     -> const ast::Expression * {
  //   auto &lspMap = lvalues[index].assigned;
  //   for (auto lspIt = lspMap.find(bounds); lspIt != lspMap.end(); lspIt++) {
  //     if (ConstantRange(lspIt.bounds()) == ConstantRange(bounds)) {
  //       return *lspIt;
  //     }
  //   }
  //   // Shouldn't get here.
  //   SLANG_UNREACHABLE;
  // }

  void handleRvalue(const ast::ValueSymbol &symbol,
                    std::pair<uint32_t, uint32_t> bounds) {
    DEBUG_PRINT("Handle R-value: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);
  }

  auto handleLvalue(const ast::ValueSymbol &symbol, const ast::Expression &lsp,
                    std::pair<uint32_t, uint32_t> bounds) {
    DEBUG_PRINT("Handle L-value: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);

    //// Lookup or create a variable node.
    // auto *node = graph.lookupVariable(symbol, bounds);
    // if (node == nullptr) {
    //   auto driver = getDriver(symbol, bounds);
    //   SLANG_ASSERT(driver.has_value() && "No driver found for L-value
    //   symbol"); node = &graph.addVariable(symbol, *driver, bounds);
    // } else {
    //   // If the node already exists, we need to update its bounds.
    //   node->as<VariableReference>().bounds = bounds;
    // }

    //// Add an edge from current state to the variable.
    auto &currState = getState();
    // if (currState.node != nullptr) {
    //   graph.addEdge(*currState.node, *node);
    // }

    // Update visited symbols to slots.
    auto [it, inserted] =
        symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());

    // Update current state assigned.
    auto index = it->second;
    if (index >= currState.assigned.size()) {
      currState.assigned.resize(index + 1);
    }

    // currState.assigned[index].unionWith(*bounds, {}, bitMapAllocator);
    ////auto &assigned = currState.assigned[index];
    // for (auto assIt = assigned.find(bounds); assIt != assigned.end();) {

    //  auto itBounds = assIt.bounds();

    //  // Existing entry completely contains new bounds.
    //  if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {
    //    // Split entry.
    //    assigned.erase(assIt, bitMapAllocator);
    //    assigned.insert({itBounds.first, bounds.first}, *assIt,
    //                    bitMapAllocator);
    //    assigned.insert({bounds.second, itBounds.second}, *assIt,
    //                    bitMapAllocator);
    //    break;
    //  }

    //  // New bounds completely contain an existing entry.
    //  if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {
    //    // Delete entry.
    //    assigned.erase(assIt, bitMapAllocator);
    //    assIt = assigned.find(bounds);
    //  } else {
    //    ++assIt;
    //  }
    //}
    // assigned.insert(bounds, node, bitMapAllocator);
    // return index;
  }

  void noteReference(const ast::ValueSymbol &symbol,
                     const ast::Expression &lsp) {
    DEBUG_PRINT("Note reference: {}\n", symbol.name);

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

  // **** AST Handlers ****

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

  // void handle(const ast::ConditionalStatement &stmt) {
  //   DEBUG_PRINT("ConditionalStatement\n");

  //  visitStmt(stmt);
  //}

  // void handle(ast::CaseStatement const &stmt) {
  //   DEBUG_PRINT("CaseStatement\n");

  //  visitStmt(stmt);
  //}

  // **** State Management ****

  void joinState(AnalysisState &result, const AnalysisState &other) {
    DEBUG_PRINT("joinState\n");
    if (result.reachable == other.reachable) {

      // Intersect assigned.
      if (result.assigned.size() > other.assigned.size()) {
        result.assigned.resize(other.assigned.size());
      }

      for (size_t i = 0; i < result.assigned.size(); i++) {

        // Determine intersecting assignments.
        auto updated =
            result.assigned[i].intersection(other.assigned[i], bitMapAllocator);

        result.assigned[i] = std::move(updated);
      }

      // Create a join node.
      // auto &node = graph.addNode(std::make_unique<Join>());
      // result.node = &node;

      // if (other.node) {
      //   graph.addEdge(*other.node, node);
      // }

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

    // Create a meet node.
    // auto &node = graph.addNode(std::make_unique<Meet>());
    // result.node = &node;

    // if (other.node != nullptr) {
    //   graph.addEdge(*other.node, node);
    // }
  }

  auto copyState(const AnalysisState &source) -> AnalysisState {
    DEBUG_PRINT("copyState\n");
    AnalysisState result;
    result.reachable = source.reachable;
    result.assigned.reserve(source.assigned.size());
    for (const auto &i : source.assigned) {
      result.assigned.emplace_back(i.clone(bitMapAllocator));
    }

    // Create a new node...
    // auto &node = graph.addNode(std::make_unique<Split>());
    // result.node = &node;
    // if (source.node != nullptr) {
    //   graph.addEdge(*source.node, node);
    // }

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
