#pragma once

#include "netlist/Utilities.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/text/FormatBuffer.h"

namespace slang::netlist {

/// Visitor for printing variable information in a human-readable format.
class ReportVariables : public ast::ASTVisitor<ReportVariables,
                                               /*VisitStatements=*/false,
                                               /*VisitExpressions=*/true,
                                               /*VisitBad=*/false,
                                               /*VisitCanonical=*/true> {
  struct VariableInfo {
    std::string name;
    SourceLocation location;
  };

  ast::Compilation &compilation;
  std::vector<VariableInfo> variables;

public:
  explicit ReportVariables(ast::Compilation &compilation)
      : compilation(compilation) {}

  /// Renders the collected variable information to the given format buffer.
  void report(FormatBuffer &buffer) {
    auto header = Utilities::Row{"Name", "Location"};
    auto table = Utilities::Table{};

    for (auto var : variables) {
      auto loc = Utilities::locationStr(compilation, var.location);
      table.push_back(Utilities::Row{var.name, loc});
    }

    Utilities::formatTable(buffer, header, table);
  }

  void handle(const ast::VariableSymbol &symbol) {
    auto variable = VariableInfo{.name = symbol.getHierarchicalPath(),
                                 .location = symbol.location};
    variables.push_back(variable);
  }
};

} // namespace slang::netlist
