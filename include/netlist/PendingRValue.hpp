#pragma once

#include "netlist/DriverMap.hpp"
#include "netlist/NetlistGraph.hpp"

#include "slang/ast/symbols/ValueSymbol.h"

namespace slang::netlist {

/// Information about a pending rvalue that needs to be processed
/// after all drivers have been visited.
struct PendingRvalue {

  // Identify the rvalue.
  not_null<const ast::ValueSymbol *> symbol;
  DriverBitRange bounds;

  // The LSP of the rvalue.
  const ast::Expression *lsp;

  // The operation in which the rvalue appears.
  NetlistNode *node{nullptr};

  PendingRvalue(const ast::ValueSymbol *symbol, const ast::Expression *lsp,
                DriverBitRange bounds, NetlistNode *node)
      : symbol(symbol), lsp(lsp), bounds(bounds), node(node) {}
};

} // namespace slang::netlist
