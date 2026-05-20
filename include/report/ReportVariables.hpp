#pragma once

#include "common/Utilities.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"

namespace slang::report {

/// Visitor for printing variable information.
class ReportVariables
    : public ast::ASTVisitor<ReportVariables, ast::VisitFlags::Expressions |
                                                  ast::VisitFlags::Canonical> {
  struct VariableInfo {
    std::string name;
    SourceLocation location;
  };

  ast::Compilation &compilation;
  std::vector<VariableInfo> variables;

public:
  explicit ReportVariables(ast::Compilation &compilation)
      : compilation(compilation) {}

  /// Render the collected variable information as a human-readable table.
  void report(FormatBuffer &buffer) {
    auto header = netlist::Utilities::Row{"Name", "Location"};
    auto table = netlist::Utilities::Table{};

    for (auto var : variables) {
      auto loc = netlist::Utilities::locationStr(compilation, var.location);
      table.push_back(netlist::Utilities::Row{var.name, loc});
    }

    netlist::Utilities::formatTable(buffer, header, table);
  }

  /// Render the collected variable information as a JSON array of objects.
  void report(JsonWriter &writer) {
    writer.startArray();
    for (auto const &var : variables) {
      writer.startObject();
      writer.writeProperty("name");
      writer.writeValue(var.name);
      writer.writeProperty("location");
      writer.writeValue(
          netlist::Utilities::locationStr(compilation, var.location));
      writer.endObject();
    }
    writer.endArray();
  }

  void handle(const ast::VariableSymbol &symbol) {
    auto variable = VariableInfo{.name = symbol.getHierarchicalPath(),
                                 .location = symbol.location};
    variables.push_back(variable);
  }
};

} // namespace slang::report
