#pragma once

#include "report/ReportVisitorBase.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/NetType.h"

namespace slang::report {

struct VariableInfo {
  std::string name;
  std::string type;
  uint64_t width;
  std::string kind;
  uint64_t drivers;
  SourceLocation location;
};

/// Visitor for printing variable / net declarations.
class ReportVariables
    : public ReportVisitorBase<ReportVariables, VariableInfo> {
  analysis::AnalysisManager &analysisManager;

  auto record(ast::ValueSymbol const &symbol, std::string kind) -> void {
    auto path = symbol.getHierarchicalPath();
    if (!nameMatches(path)) {
      return;
    }
    items.push_back(VariableInfo{
        .name = std::move(path),
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
      : ReportVisitorBase(compilation), analysisManager(analysisManager) {}

  auto tableHeader() const -> netlist::Utilities::Row {
    return {"Name", "Type", "Width", "Kind", "Drivers", "Location"};
  }

  void appendItemRows(netlist::Utilities::Table &table,
                      VariableInfo const &var) const {
    table.push_back(netlist::Utilities::Row{
        var.name, var.type, std::to_string(var.width), var.kind,
        std::to_string(var.drivers), locationStr(var.location)});
  }

  void emitJsonItem(JsonWriter &writer, VariableInfo const &var) const {
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
    writer.writeValue(locationStr(var.location));
    writer.endObject();
  }

  void handle(const ast::VariableSymbol &symbol) { record(symbol, "var"); }

  void handle(const ast::NetSymbol &symbol) {
    record(symbol, std::string(symbol.netType.name));
  }
};

} // namespace slang::report
