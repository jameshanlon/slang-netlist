#pragma once

#include "netlist/ReportingUtilities.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/text/FormatBuffer.h"

namespace slang::netlist {

/// Visitor for printing symbol information in a human-readable format.
class ReportVariables : public ast::ASTVisitor<ReportVariables,
                                               /*VisitStatements=*/false,
                                               /*VisitExpressions=*/true,
                                               /*VisitBad=*/false,
                                               /*VisitCanonical=*/true> {
  ast::Compilation &compilation;
  FormatBuffer &buffer;

public:
  explicit ReportVariables(ast::Compilation &compilation, FormatBuffer &buffer)
      : compilation(compilation), buffer(buffer) {}

  void handle(const ast::VariableSymbol &symbol) {
    buffer.append(fmt::format(
        "Value {} {} {}\n", symbol.name, symbol.getHierarchicalPath(),
        ReportingUtilities::locationStr(compilation, symbol.location)));
  }
};

} // namespace slang::netlist
