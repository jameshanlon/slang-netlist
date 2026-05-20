#pragma once

#include "common/Utilities.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/NetType.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"

namespace slang::report {

/// Visitor for printing variable / net declarations.
class ReportVariables
    : public ast::ASTVisitor<ReportVariables, ast::VisitFlags::Expressions |
                                                  ast::VisitFlags::Canonical> {
  struct VariableInfo {
    std::string name;
    std::string type;
    uint64_t width;
    std::string kind;
    uint64_t drivers;
    SourceLocation location;
  };

  ast::Compilation &compilation;
  analysis::AnalysisManager &analysisManager;
  std::vector<VariableInfo> variables;

  auto record(ast::ValueSymbol const &symbol, std::string kind) -> void {
    variables.push_back(VariableInfo{
        .name = symbol.getHierarchicalPath(),
        .type = symbol.getType().toString(),
        .width = symbol.getType().getBitWidth(),
        .kind = std::move(kind),
        .drivers = analysisManager.getDrivers(symbol).size(),
        .location = symbol.location,
    });
  }

public:
  explicit ReportVariables(ast::Compilation &compilation,
                           analysis::AnalysisManager &analysisManager)
      : compilation(compilation), analysisManager(analysisManager) {}

  /// Render the collected variable information as a human-readable table.
  void report(FormatBuffer &buffer) {
    auto header = netlist::Utilities::Row{"Name", "Type",    "Width",
                                          "Kind", "Drivers", "Location"};
    auto table = netlist::Utilities::Table{};

    for (auto const &var : variables) {
      auto loc = netlist::Utilities::locationStr(compilation, var.location);
      table.push_back(
          netlist::Utilities::Row{var.name, var.type, std::to_string(var.width),
                                  var.kind, std::to_string(var.drivers), loc});
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
      writer.writeProperty("type");
      writer.writeValue(var.type);
      writer.writeProperty("width");
      writer.writeValue(var.width);
      writer.writeProperty("kind");
      writer.writeValue(var.kind);
      writer.writeProperty("drivers");
      writer.writeValue(var.drivers);
      writer.writeProperty("location");
      writer.writeValue(
          netlist::Utilities::locationStr(compilation, var.location));
      writer.endObject();
    }
    writer.endArray();
  }

  void handle(const ast::VariableSymbol &symbol) { record(symbol, "var"); }

  void handle(const ast::NetSymbol &symbol) {
    record(symbol, std::string(symbol.netType.name));
  }
};

} // namespace slang::report
