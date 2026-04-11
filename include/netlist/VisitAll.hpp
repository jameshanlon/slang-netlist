#pragma once

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"

#include <cstdint>

namespace slang::netlist {

/// Visitor to visit the entire AST prior to freezing. This is required since
/// AST construction is lazy, so visiting a previously univisted node can cause
/// modifications, which is not threadsafe.  This allows the subsequent netlist
/// construction pass to be multithreaded, in the same way Slang's analysis pass
/// is.
struct VisitAll : public ast::ASTVisitor<VisitAll, ast::VisitFlags::AllGood> {
  uint64_t count = 0;

  void handle(const ast::ValueSymbol &symbol) { count++; }

  /// Skip uninstantiated instances to avoid forcing lazy elaboration of
  /// modules that are not part of the design hierarchy (e.g. when --top
  /// selects a specific root module).
  void handle(const ast::InstanceSymbol &symbol) {
    if (!symbol.body.flags.has(ast::InstanceFlags::Uninstantiated)) {
      visitDefault(symbol);
    }
  }
};

} // namespace slang::netlist
