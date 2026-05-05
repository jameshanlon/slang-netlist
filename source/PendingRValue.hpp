#pragma once

#include "netlist/NetlistGraph.hpp"

#include "slang/ast/SemanticFacts.h"
#include "slang/ast/symbols/ValueSymbol.h"

#include <utility>

namespace slang::netlist {

/// Record information about a pending rvalue that needs to be processed
/// after all drivers have been visited.
struct PendingRvalue {

  // Identify the rvalue.
  not_null<const ast::ValueSymbol *> symbol;
  DriverBitRange bounds;

  // The longest static prefix expression of the rvalue.
  const ast::Expression *lsp;

  // The operation in which the rvalue appears.
  NetlistNode *node{nullptr};

  // Edge kind to stamp on the resolved edge. Non-None marks this rvalue
  // as a clocking/reset signal from an event list.
  ast::EdgeKind edgeKind{ast::EdgeKind::None};

  PendingRvalue(const ast::ValueSymbol *symbol, const ast::Expression *lsp,
                DriverBitRange bounds, NetlistNode *node,
                ast::EdgeKind edgeKind = ast::EdgeKind::None)
      : symbol(symbol), lsp(lsp), bounds(std::move(bounds)), node(node),
        edgeKind(edgeKind) {}
};

} // namespace slang::netlist
