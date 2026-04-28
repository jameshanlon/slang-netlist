#include "DataFlowAnalysis.hpp"
#include "BitSliceList.hpp"
#include "DriverMap.hpp"
#include "NetlistBuilder.hpp"

#include "slang/ast/Expression.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"

namespace slang::netlist {

void DataFlowAnalysis::addNonBlockingLvalue(ast::ValueSymbol const &symbol,
                                            ast::Expression const &lsp,
                                            DriverBitRange bounds,
                                            NetlistNode *node) {
  DEBUG_PRINT("Adding pending non-blocking L-value: {}{}\n", symbol.name,
              toString(bounds));
  pendingLValues.emplace_back(&symbol, &lsp, bounds, node);
}

void DataFlowAnalysis::processNonBlockingLvalues() {
  for (auto &pending : pendingLValues) {
    DEBUG_PRINT("Processing pending non-blocking L-value: {}{}\n",
                pending.symbol->name, toString(pending.bounds));
    valueTracker.addDrivers(getState().valueDrivers, *pending.symbol,
                            pending.bounds,
                            {DriverInfo(pending.node, pending.lsp)});
  }
  pendingLValues.clear();
}

void DataFlowAnalysis::handleRvalue(ast::ValueSymbol const &symbol,
                                    ast::Expression const &lsp,
                                    DriverBitRange bounds) {
  DEBUG_PRINT("Handle R-value: {}{}\n", symbol.name, toString(bounds));
  auto &currState = getState();

  // Initialise a new interval map for the R-value to track which parts of it
  // have been assigned within this procedural block.
  DriverMap rvalueMap;

  // Set up the R-value map to cover the entire bounds.
  auto newHandle = rvalueMap.newDriverList();
  rvalueMap.getDriverList(newHandle).emplace(nullptr, nullptr);
  rvalueMap.insert(bounds, newHandle, rvalueMapAllocator);

  auto symbolSlot = valueTracker.getSlot(symbol);

  if (!symbolSlot.has_value() ||
      *symbolSlot >= getState().valueDrivers.size()) {
    // No definitions for this symbol yet, so nothing to do.
    DEBUG_PRINT("No definitions for symbol {}, adding to pending list.\n",
                symbol.name);
    auto *node = currState.node != nullptr ? currState.node : externalNode;
    builder.addRvalue(getEvalContext(), symbol, lsp, bounds, node);
    return;
  }

  auto &definitions = currState.valueDrivers[*symbolSlot];

  // If there is no current control flow node (eg. inside a conditional with
  // constant conditions), we cannot add edges directly. Fall back to the
  // pending R-value list, which will be resolved after all drivers are visited.
  if (currState.node == nullptr) {
    builder.addRvalue(getEvalContext(), symbol, lsp, bounds, externalNode);
    return;
  }

  // Hoist the SymbolReference construction out of the per-interval loop; it
  // depends only on the symbol and is otherwise a hot source of repeated
  // hierarchicalPath/FileTable work during DFA.
  auto symbolRef = builder.toSymbolRef(symbol);

  for (auto it = definitions.find(bounds); it != definitions.end(); it++) {

    auto itBounds = it.bounds();
    auto handle = *it;
    auto &driverList = definitions.getDriverList(handle);

    // Definition bounds completely contains R-value bounds.
    // Ie. the definition covers the R-value.
    //   Rvalue       |----|
    //   Definition |----------|
    if (ConstantRange(itBounds).contains(bounds)) {

      // Add an edge from the definition node to the current node
      // using it.
      SLANG_ASSERT(currState.node != nullptr);
      builder.addDriversToNode(driverList, *currState.node, symbolRef, bounds);

      // All done, exit early.
      return;
    }

    // R-value bounds completely contain a definition bounds.
    // Ie. a definition contributes to the R-value.
    //   Rvalue     |----------|
    //   Definition   |----|
    if (bounds.contains(ConstantRange(itBounds))) {

      // Add an edge from the definition node to the current node
      // using it.
      builder.addDriversToNode(driverList, *currState.node, symbolRef, bounds);

      // Examine the next definition in the next iteration.
    }
  }

  // Calculate the difference between the R-value map and the
  // definitions provided in this procedural block. That leaves the
  // parts of the R-value that are defined outside of this procedural
  // block.
  rvalueMap.driverIntervals = IntervalMapUtils::difference(
      rvalueMap.driverIntervals, definitions.driverIntervals,
      valueTracker.getAllocator());

  // If we get to this point, rvalueMap holds the intervals of the R-value
  // that are assigned outside of this procedural block. Then, we
  // add a pending R-values to the list of pending ones to be
  // processed after all drivers have been visited.

  for (auto it = rvalueMap.begin(); it != rvalueMap.end(); ++it) {
    auto itBounds = it.bounds();
    auto *node = currState.node != nullptr ? currState.node : externalNode;
    builder.addRvalue(getEvalContext(), symbol, lsp,
                      {itBounds.first, itBounds.second}, node);
  }
}

void DataFlowAnalysis::finalize() { processNonBlockingLvalues(); }

void DataFlowAnalysis::handleLvalue(ast::ValueSymbol const &symbol,
                                    ast::Expression const &lsp,
                                    DriverBitRange bounds) {
  DEBUG_PRINT("Handle lvalue: {}{}\n", symbol.name, toString(bounds));

  // If this is a non-blocking assignment, then the assignment occurs at the
  // end of the block and so the result is not visible within the block.
  // However, the definition may still be used in the block as an initial
  // R-value.

  if (!isBlocking) {
    addNonBlockingLvalue(symbol, lsp, bounds, getState().node);
    return;
  }

  valueTracker.addDrivers(getState().valueDrivers, symbol, bounds,
                          {DriverInfo(getState().node, &lsp)});
}

/// As per DataFlowAnalysis in upstream slang, but with custom handling of
/// L- and R-values.
void DataFlowAnalysis::noteReference(ast::ValuePath const &path) {

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

  if (path.empty() || !path.lsp) {
    return;
  }

  auto const *rootSymbol = path.rootSymbol();
  if (rootSymbol == nullptr) {
    return;
  }
  auto const &symbol = *rootSymbol;

  // Skip automatic variables.
  if (ast::VariableSymbol::isKind(symbol.kind) &&
      symbol.as<ast::VariableSymbol>().lifetime ==
          ast::VariableLifetime::Automatic) {
    return;
  }

  auto bounds = DriverBitRange(path.lspBounds);
  auto const &lsp = *path.lsp;

  if (isLValue) {
    handleLvalue(symbol, lsp, bounds);
  } else {
    handleRvalue(symbol, lsp, bounds);
  }
}

void DataFlowAnalysis::updateNode(NetlistNode *node, bool conditional) {
  auto &currState = getState();

  // If there is a previous conditional node, then add an edge
  if (currState.condition != nullptr) {
    builder.addDependency(*currState.condition, *node);
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

void DataFlowAnalysis::handle(ast::ProceduralAssignStatement const &stmt) {
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

void DataFlowAnalysis::handleAssignmentLegacy(
    ast::AssignmentExpression const &expr) {
  auto &node = builder.createAssignment(expr);
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

void DataFlowAnalysis::handle(ast::AssignmentExpression const &expr) {
  DEBUG_PRINT("AssignmentExpression\n");

  if (!builder.options.resolveAssignBits) {
    handleAssignmentLegacy(expr);
    return;
  }

  // Bit-aligned decomposition only makes sense for integral types whose
  // selectable width is well-defined. Strings, unpacked aggregates, and
  // other dynamically-sized types all fall through to the legacy walk
  // cheaply rather than paying for a throwaway slicelist build.
  if (!expr.left().type->isIntegral() || !expr.right().type->isIntegral()) {
    handleAssignmentLegacy(expr);
    return;
  }

  // Bit-aligned path. Pass the cut registry so LSPs split at cuts
  // propagated from external concats at port boundaries.
  CutRegistry const *cuts =
      builder.options.propCutsAcrossPorts ? &builder.cutRegistry : nullptr;
  auto lhsList = BitSliceList::build(expr.left(), getEvalContext(),
                                     sliceAllocator, /*enabled=*/true, cuts);
  auto rhsList = BitSliceList::build(expr.right(), getEvalContext(),
                                     sliceAllocator, /*enabled=*/true, cuts);
  // The two sides can still disagree on selectable width even when both
  // are integral (e.g. an enum-to-logic coercion at a port connection
  // re-parented here). Fall back rather than asserting.
  if (lhsList.width() != rhsList.width()) {
    handleAssignmentLegacy(expr);
    return;
  }

  auto *savedNode = getState().node;
  auto *savedCondition = getState().condition;
  isBlocking = expr.isBlocking();

  for (auto const &seg : alignSegments(lhsList, rhsList)) {
    // Reset per-segment flow state to the pre-segment snapshot so each
    // segment's Assignment node gets its own control edge from the
    // enclosing condition rather than chaining off the previous segment.
    getState().node = savedNode;
    getState().condition = savedCondition;

    auto &segNode = builder.createAssignment(expr);
    updateNode(&segNode, false);

    // Drive LHS: every LSP source emits an lvalue note with the mapped
    // sub-range on its root symbol.
    if (!prohibitLValue) {
      SLANG_ASSERT(!isLValue);
      isLValue = true;
      for (auto const &src : seg.lhsSources) {
        if (src.kind == BitSliceSource::Kind::Lsp) {
          driveLhsLspSegment(src, seg);
        }
        // Padding/Opaque on LHS is unreachable — slang rejects them.
      }
      isLValue = false;
    }

    // Drive RHS: LSPs and opaque sub-visits attach to this segment's node.
    if (!expr.isLValueArg()) {
      for (auto const &src : seg.rhsSources) {
        switch (src.kind) {
        case BitSliceSource::Kind::Lsp:
          driveRhsLspSegment(src, seg);
          break;
        case BitSliceSource::Kind::Opaque: {
          auto savedLVal = isLValue;
          isLValue = false;
          visit(*src.opaqueExpr);
          isLValue = savedLVal;
          break;
        }
        case BitSliceSource::Kind::Padding:
          // No driver.
          break;
        case BitSliceSource::Kind::Constant: {
          if (getState().node == nullptr) {
            break;
          }
          auto &constNode = builder.createConstantForSegment(
              src, seg, builder.toTextLocation(expr.sourceRange.start()));
          builder.addDependency(constNode, *getState().node);
          break;
        }
        case BitSliceSource::Kind::PortNode:
          // Not valid on the RHS of an assignment.
          SLANG_UNREACHABLE;
        }
      }
    }
  }

  // Leave getState().node pointing at the last segment's Assignment so
  // state-merging at control-flow joins can connect this branch's
  // assignment to the merge point. Legacy handleAssignmentLegacy relies
  // on the same invariant via its single createAssignment.
}

void DataFlowAnalysis::handle(ast::ConditionalStatement const &stmt) {
  DEBUG_PRINT("ConditionalStatement\n");

  // If all conditions are constant, then there is no need to include this
  if (std::all_of(stmt.conditions.begin(), stmt.conditions.end(),
                  [&](ast::ConditionalStatement::Condition const &cond)
                      -> ConstantValue { return tryEvalBool(*cond.expr); })) {
    visitStmt(stmt);
    return;
  }

  auto &node = builder.createConditional(stmt);
  updateNode(&node, true);
  visitStmt(stmt);
}

void DataFlowAnalysis::handle(ast::CaseStatement const &stmt) {
  DEBUG_PRINT("CaseStatement\n");
  auto &node = builder.createCase(stmt);
  updateNode(&node, true);
  visitStmt(stmt);
}

auto DataFlowAnalysis::mergeStates(AnalysisState &result,
                                   AnalysisState const &other) {

  // Merge in other definitions to result.
  for (auto i = 0; i < other.valueDrivers.size(); i++) {
    DEBUG_PRINT("Merging symbol at index {}\n", i);
    auto const *symbol = valueTracker.getSymbol(i);
    for (auto it = other.valueDrivers[i].begin();
         it != other.valueDrivers[i].end(); it++) {
      auto bounds = it.bounds();
      auto const &driverList = other.valueDrivers[i].getDriverList(*it);
      DEBUG_PRINT("Inserting b {}\n", toString(bounds));
      valueTracker.addDrivers(result.valueDrivers, *symbol, bounds, driverList,
                              /*merge=*/true);
    }
  }

  auto mergeNodes = [&](NetlistNode *a, NetlistNode *b) -> NetlistNode * {
    if (a != nullptr && b != nullptr && a != b) {
      // If the nodes are different, then we need to create a new
      // node.
      return &builder.merge(*a, *b);
    }

    if (b == nullptr) {
      // Otherwise, just use a node.
      return a;
    }

    if (a == nullptr) {
      // Otherwise, just use b node.
      return b;
    }

    // If both nodes are null, then we don't need to set the node.
    return nullptr;
  };

  // Node pointers.
  result.node = mergeNodes(result.node, other.node);
  result.condition = mergeNodes(result.condition, other.condition);

  DEBUG_PRINT("Merged states: a.defs.size={}, b.defs.size={}, "
              "result.defs.size={}\n",
              result.valueDrivers.size(), other.valueDrivers.size(),
              result.valueDrivers.size());
}

void DataFlowAnalysis::joinState(AnalysisState &result,
                                 AnalysisState const &other) {
  DEBUG_PRINT("joinState\n");
  if (result.reachable == other.reachable) {
    mergeStates(result, other);
  } else if (!result.reachable) {
    result = copyState(other);
  }
}

void DataFlowAnalysis::meetState(AnalysisState &result,
                                 AnalysisState const &other) {
  DEBUG_PRINT("meetState\n");
  if (!other.reachable) {
    result.reachable = false;
    return;
  }
  mergeStates(result, other);
}

auto DataFlowAnalysis::copyState(AnalysisState const &source) -> AnalysisState {
  DEBUG_PRINT("copyState\n");
  AnalysisState result;
  result.reachable = source.reachable;
  result.node = source.node;
  result.condition = source.condition;
  result.valueDrivers.reserve(source.valueDrivers.size());
  for (const auto &definition : source.valueDrivers) {
    result.valueDrivers.emplace_back(
        definition.clone(valueTracker.getAllocator()));
  }
  return result;
}

auto DataFlowAnalysis::unreachableState() -> AnalysisState {
  DEBUG_PRINT("unreachableState\n");
  AnalysisState result;
  result.reachable = false;
  return result;
}

auto DataFlowAnalysis::topState() -> AnalysisState { return {}; }

void DataFlowAnalysis::driveLhsLspSegment(const BitSliceSource &src,
                                          const Segment &seg) {
  SLANG_ASSERT(src.kind == BitSliceSource::Kind::Lsp);
  SLANG_ASSERT(seg.concatLo >= src.srcLo);
  auto const &path = *src.path;
  auto const &symbol = *path.rootSymbol();
  auto const *lsp = path.lsp;
  // Offset of this segment's LSB within the LSP's concat range.
  auto offset = seg.concatLo - src.srcLo;
  auto width = seg.width();
  auto lo = static_cast<int32_t>(path.lspBounds.first + offset);
  // `width - 1` because `DriverBitRange` is inclusive on both ends.
  auto hi = static_cast<int32_t>(path.lspBounds.first + offset + width - 1);
  DriverBitRange bounds{lo, hi};
  handleLvalue(symbol, *lsp, bounds);
}

void DataFlowAnalysis::driveRhsLspSegment(const BitSliceSource &src,
                                          const Segment &seg) {
  SLANG_ASSERT(src.kind == BitSliceSource::Kind::Lsp);
  SLANG_ASSERT(seg.concatLo >= src.srcLo);
  auto const &path = *src.path;
  auto const &symbol = *path.rootSymbol();
  auto const *lsp = path.lsp;
  // Offset of this segment's LSB within the LSP's concat range.
  auto offset = seg.concatLo - src.srcLo;
  auto width = seg.width();
  auto lo = static_cast<int32_t>(path.lspBounds.first + offset);
  // `width - 1` because `DriverBitRange` is inclusive on both ends.
  auto hi = static_cast<int32_t>(path.lspBounds.first + offset + width - 1);
  DriverBitRange bounds{lo, hi};
  handleRvalue(symbol, *lsp, bounds);
}

} // namespace slang::netlist
