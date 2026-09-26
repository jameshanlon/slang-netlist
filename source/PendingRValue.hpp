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

/// A driver awaiting connection to the node that represents a variable's
/// storage over @p bounds, resolved once every block has contributed its
/// nodes so the lookup sees the same candidates whatever order the blocks
/// were processed in.
struct PendingVariableHookup {

  // The driving operation.
  NetlistNode *driver;

  // The variable whose node the driver connects to, and the range of it
  // that the lookup must match.
  not_null<const ast::Symbol *> variable;
  DriverBitRange bounds;

  // Annotation for the resulting edge. Names the symbol the driver
  // writes, which for a modport connection differs from the one
  // looked up.
  SymbolReference const *edgeSymbol;
};

} // namespace slang::netlist
