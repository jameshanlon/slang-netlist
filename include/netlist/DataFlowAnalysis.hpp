#pragma once

#include "netlist/Debug.hpp"
#include "netlist/IntervalMapUtils.hpp"
#include "netlist/NetlistGraph.hpp"

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/util/BumpAllocator.h"
#include "slang/util/IntervalMap.h"

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

struct PendingLvalue {
  const ast::ValueSymbol *symbol;
  std::pair<uint64_t, uint64_t> bounds;
  NetlistNode *node{nullptr};

  PendingLvalue(const ast::ValueSymbol *symbol,
                std::pair<uint64_t, uint64_t> bounds, NetlistNode *node)
      : symbol(symbol), bounds(bounds), node(node) {}
};

/// A data flow analysis used as part of the netlist graph construction.
struct DataFlowAnalysis
    : public analysis::AbstractFlowAnalysis<DataFlowAnalysis, AnalysisState> {

  using ParentAnalysis =
      analysis::AbstractFlowAnalysis<DataFlowAnalysis, AnalysisState>;

  friend class AbstractFlowAnalysis;

  template <typename TOwner> friend struct ast::LSPVisitor;

  analysis::AnalysisManager &analysisManager;

  BumpAllocator allocator;
  SymbolDriverMap::allocator_type bitMapAllocator;
  SymbolLSPMap::allocator_type lspMapAllocator;

  // Maps visited symbols to slots in definitions vectors.
  SymbolSlotMap symbolToSlot;

  // Maps slots to symbols for labelling graph merge edges.
  std::vector<const ast::ValueSymbol *> slotToSymbol;

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
                   const ast::Symbol &symbol, NetlistGraph &graph,
                   NetlistNode *externalNode = nullptr)
      : AbstractFlowAnalysis(symbol, {}), analysisManager(analysisManager),
        bitMapAllocator(allocator), lspMapAllocator(allocator),
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

<<<<<<< HEAD
  /// Update the current state definitions for an L-value symbol with the
  /// specified bounds.
  auto updateDefinitions(ast::ValueSymbol const &symbol,
                         std::pair<uint64_t, uint64_t> bounds,
                         NetlistNode *node) -> void {

    auto &currState = getState();

    // Update visited symbols to slots.
    auto [it, inserted] =
        symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());

    auto index = it->second;

    // Resize definitions vector if necessary.
    if (index >= currState.definitions.size()) {
      currState.definitions.resize(index + 1);
    }

    // Resize slotToSymbol vector if necessary.
    if (index >= slotToSymbol.size()) {
      slotToSymbol.resize(index + 1);
      slotToSymbol[index] = &symbol;
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
    definitions.insert(bounds, node, bitMapAllocator);
  }

  /// Add a non-blocking L-value to a pending list to be processed at the end of
  /// the block.
  auto addNonBlockingLvalue(const ast::ValueSymbol *symbol,
                            std::pair<uint64_t, uint64_t> bounds,
                            NetlistNode *node) -> void {
    DEBUG_PRINT("Adding pending non-blocking L-value: {} [{}:{}]\n",
                symbol->name, bounds.first, bounds.second);
    SLANG_ASSERT(symbol != nullptr && "Symbol must not be null");
    pendingLValues.emplace_back(symbol, bounds, node);
  }

  /// Process all pending non-blocking L-values by updating the final
  /// definitions of the block.
  auto processNonBlockingLvalues() {
    for (auto &pending : pendingLValues) {
      DEBUG_PRINT("Processing pending non-blocking L-value: {} [{}:{}]\n",
                  pending.symbol->name, pending.bounds.first,
                  pending.bounds.second);
      updateDefinitions(*pending.symbol, pending.bounds, pending.node);
    }
    pendingLValues.clear();
  }

||||||| parent of afc823d (Tidy up comments)
=======
  //===---------------------------------------------------------===//
  // L- and R-value handling.
  //===---------------------------------------------------------===//

>>>>>>> afc823d (Tidy up comments)
  void handleRvalue(const ast::ValueSymbol &symbol,
                    std::pair<uint32_t, uint32_t> bounds) {
    DEBUG_PRINT("Handle R-value: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);

    // Initiliase a new interval map for the R-value to track
    // which parts of it have been assigned within this procedural block.
    IntervalMap<uint64_t, NetlistNode *, 8> rvalueMap;
    BumpAllocator ba;
    IntervalMap<int32_t, int32_t>::allocator_type alloc(ba);
    rvalueMap.insert(bounds, nullptr, alloc);

    if (symbolToSlot.contains(&symbol)) {
      // Symbol is assigned in this procedural block.

      auto &currState = getState();
      auto index = symbolToSlot.at(&symbol);

      if (currState.definitions.size() <= index) {
        // There are no definitions for this symbol on the current control path,
        // but definition(s) do exist on other control paths. This occurs when
        // the symbol is sequential and the definition is created on a previous
        // edge (ie sequential).
        DEBUG_PRINT("No definition for symbol {} at index {}, adding to "
                    "pending list.\n",
                    symbol.name, index);
        graph.addRvalue(&symbol, bounds, currState.node);
        return;
      }

      auto &definitions = currState.definitions[index];

      for (auto it = definitions.find(bounds); it != definitions.end(); it++) {

        auto itBounds = it.bounds();
        auto &currState = getState();

        // Definition bounds completely contains R-value bounds.
        // Ie. the definition covers the R-value.
        if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {

          // Add an edge from the definition node to the current node
          // using it.
          if (currState.node) {
            auto &edge = graph.addEdge(**it, *currState.node);
            edge.setVariable(&symbol, bounds);
          }

          // All done, exit early.
          return;
        }

        // R-value bounds completely contain a definition bounds.
        // Ie. a definition contributes to the R-value.
        if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {

          // Add an edge from the definition node to the current node
          // using it.
          SLANG_ASSERT(currState.node);
          auto &edge = graph.addEdge(**it, *currState.node);
          edge.setVariable(&symbol, bounds);
        }
      }

      // Calculate the difference between the R-value map and the
      // definitions provided in this procedural block. That leaves the
      // parts of the R-value that are defined outside of this procedural
      // block.
      rvalueMap = IntervalMapUtils::difference(rvalueMap, definitions, alloc);
    }

    // If we get to this point, rvalueMap hold the intervals of the R-value
    // that are assigned outside of this procedural block.  In this case, we
    // just add a pending R-value to the list of pending R-values to be
    // processed after all drivers have been visited.
    auto &currState = getState();
    auto *node = currState.node != nullptr ? currState.node : externalNode;
    for (auto it = rvalueMap.begin(); it != rvalueMap.end(); ++it) {
      auto itBounds = it.bounds();
      graph.addRvalue(&symbol, {itBounds.first, itBounds.second}, node);
    }
  }

  /// Finalize the analysis by processing any pending non-blocking L-values.
  /// This should be called after the main analysis has completed.
  auto finalize() { processNonBlockingLvalues(); }

  auto handleLvalue(const ast::ValueSymbol &symbol, const ast::Expression &lsp,
                    std::pair<uint32_t, uint32_t> bounds) {

    DEBUG_PRINT("Handle lvalue: {} [{}:{}]\n", symbol.name, bounds.first,
                bounds.second);

    // If this is a non-blocking assignment, then the assignment occurs at the
    // end of the block and so the result is not visible within the block.
    // However, the definition may still be used in the block as an initial
    // R-value.
    if (!isBlocking) {
      addNonBlockingLvalue(&symbol, bounds, getState().node);
      return;
    }

    updateDefinitions(symbol, bounds, getState().node);
  }

  /// As per DataFlowAnalysis in upstream slang, but with custom handling of
  /// L- and R-values. Called by the LSP visitor.
  void noteReference(const ast::ValueSymbol &symbol,
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
      handleRvalue(symbol, *bounds);
    }
  }

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

  void updateNode(NetlistNode *node, bool conditional) {
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

  void handle(const ast::ProceduralAssignStatement &stmt) {
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

  void handle(const ast::AssignmentExpression &expr) {
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

  void handle(ast::ConditionalStatement const &stmt) {
    DEBUG_PRINT("ConditionalStatement\n");

    // If all conditons are constant, then there is no need to include this
    // as a node.
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

  void handle(ast::CaseStatement const &stmt) {
    DEBUG_PRINT("CaseStatement\n");

    auto &node = graph.addNode(std::make_unique<Case>(stmt));

    updateNode(&node, true);

    visitStmt(stmt);
  }

  //===---------------------------------------------------------===//
  // State management
  //===---------------------------------------------------------===//

  [[nodiscard]] auto mergeStates(AnalysisState const &a,
                                 AnalysisState const &b) {

    AnalysisState result;
    result.definitions.resize(
        std::max(a.definitions.size(), b.definitions.size()));

    // For each symbol, merge intervals from a and b.
    for (size_t i = 0; i < result.definitions.size(); i++) {

      if (i >= a.definitions.size() && i < b.definitions.size()) {
        // No definitions for this symbol in a, but there are in b.
        result.definitions[i] = b.definitions[i].clone(bitMapAllocator);
        continue;
      }

      if (i >= b.definitions.size() && i < a.definitions.size()) {
        // No definitions for this symbol in b, but there are in a.
        result.definitions[i] = a.definitions[i].clone(bitMapAllocator);
        continue;
      }

      for (auto aIt = a.definitions[i].begin(); aIt != a.definitions[i].end();
           ++aIt) {

        for (auto bIt = b.definitions[i].begin(); bIt != b.definitions[i].end();
             ++bIt) {

          auto aBounds = aIt.bounds();
          auto bBounds = bIt.bounds();

          if (aBounds == bBounds) {
            // Bounds are equal, so merge the nodes.
            auto &node = graph.addNode(std::make_unique<Merge>());

            auto &edgea = graph.addEdge(**aIt, node);
            edgea.setVariable(slotToSymbol[i], aBounds);

            auto &edgeb = graph.addEdge(**bIt, node);
            edgeb.setVariable(slotToSymbol[i], bBounds);

            result.definitions[i].insert(aBounds, &node, bitMapAllocator);
          } else if (ConstantRange(aBounds).overlaps(ConstantRange(bBounds))) {
            // If the bounds overlap, we need to create a new node
            // that represents the shared interval that is merged.
            // We also need to handle non-overlapping left and right
            // hand side intervals.

            // Left part.
            if (aBounds.first < bBounds.first) {
              result.definitions[i].insert({aBounds.first, bBounds.first}, *aIt,
                                           bitMapAllocator);
            }

            if (bBounds.first < aBounds.first) {
              result.definitions[i].insert({bBounds.first, aBounds.first}, *bIt,
                                           bitMapAllocator);
            }

            // Right part.
            if (aBounds.second > bBounds.second) {
              result.definitions[i].insert({bBounds.second, aBounds.second},
                                           *aIt, bitMapAllocator);
            }

            if (bBounds.second > aBounds.second) {
              result.definitions[i].insert({aBounds.second, bBounds.second},
                                           *bIt, bitMapAllocator);
            }

            // Middle part.
            auto &node = graph.addNode(std::make_unique<Merge>());

            auto &edgea = graph.addEdge(**aIt, node);
            edgea.setVariable(slotToSymbol[i], aBounds);

            auto &edgeb = graph.addEdge(**bIt, node);
            edgeb.setVariable(slotToSymbol[i], bBounds);

            result.definitions[i].insert(bBounds, &node, bitMapAllocator);
          } else {
            // If the bounds do not overlap, just insert both.
            result.definitions[i].insert(aBounds, *aIt, bitMapAllocator);
            result.definitions[i].insert(bBounds, *bIt, bitMapAllocator);
          }
        }
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

  void joinState(AnalysisState &result, const AnalysisState &other) {
    DEBUG_PRINT("joinState\n");
    if (result.reachable == other.reachable) {
      result = mergeStates(result, other);
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
    result = mergeStates(result, other);
  }

  auto copyState(const AnalysisState &source) -> AnalysisState {
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

  static auto unreachableState() -> AnalysisState {
    DEBUG_PRINT("unreachableState\n");
    AnalysisState result;
    result.reachable = false;
    return result;
  }

  static auto topState() -> AnalysisState { return {}; }
};

} // namespace slang::netlist
