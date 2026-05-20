#pragma once

#include "netlist/DriverBitRange.hpp"
#include "report/ReportVisitorBase.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/symbols/ValueSymbol.h"

namespace slang::report {

struct DriverInfo {
  std::string prefix;
  analysis::DriverKind kind;
  netlist::DriverBitRange bounds;
  SourceLocation location;
};

struct ValueInfo {
  std::string path;
  SourceLocation location;
  std::vector<DriverInfo> drivers;
};

/// Visitor for printing driver information.
class ReportDrivers : public ReportVisitorBase<ReportDrivers, ValueInfo> {
  analysis::AnalysisManager &analysisManager;

  /// Return a string representation of the LSP for a driver of a symbol.
  static auto driverPathToString(const ast::ValueSymbol &symbol,
                                 const analysis::ValueDriver &driver) {
    ast::EvalContext evalContext(symbol);
    return driver.path.toString(evalContext);
  }

  static auto driverKindStr(analysis::DriverKind kind) -> std::string_view {
    return kind == analysis::DriverKind::Procedural ? "proc" : "cont";
  }

public:
  explicit ReportDrivers(ast::Compilation &compilation,
                         analysis::AnalysisManager &analysisManager)
      : ReportVisitorBase(compilation), analysisManager(analysisManager) {}

  auto tableHeader() const -> netlist::Utilities::Row {
    return {"Value", "Range", "Driver", "Type", "Location"};
  }

  void appendItemRows(netlist::Utilities::Table &table,
                      ValueInfo const &value) const {
    table.push_back(netlist::Utilities::Row{value.path, "", "", "",
                                            locationStr(value.location)});
    for (auto const &driver : value.drivers) {
      table.push_back(
          netlist::Utilities::Row{"↳", toString(driver.bounds), driver.prefix,
                                  std::string(driverKindStr(driver.kind)),
                                  locationStr(driver.location)});
    }
  }

  void emitJsonItem(JsonWriter &writer, ValueInfo const &value) const {
    writer.startObject();
    writer.writeProperty("value");
    writer.writeValue(value.path);
    writer.writeProperty("location");
    writer.writeValue(locationStr(value.location));
    writer.writeProperty("drivers");
    writer.startArray();
    for (auto const &driver : value.drivers) {
      writer.startObject();
      writer.writeProperty("range");
      writer.writeValue(toString(driver.bounds));
      writer.writeProperty("driver");
      writer.writeValue(driver.prefix);
      writer.writeProperty("kind");
      writer.writeValue(driverKindStr(driver.kind));
      writer.writeProperty("location");
      writer.writeValue(locationStr(driver.location));
      writer.endObject();
    }
    writer.endArray();
    writer.endObject();
  }

  /// Slang's AnalysisManager::getDrivers API returns all known drivers for
  /// static lvalue symbols (via the ValueSymbol type). Create a ValueInfo
  /// entry for each symbol and populate it with the driver information.
  void handle(ast::ValueSymbol const &symbol) {
    if (!nameMatches(symbol.name)) {
      return;
    }
    auto value = ValueInfo{.path = symbol.getHierarchicalPath(),
                           .location = symbol.location,
                           .drivers = {}};

    auto drivers = analysisManager.getDrivers(symbol);
    for (auto const *driver : drivers) {
      value.drivers.emplace_back(driverPathToString(symbol, *driver),
                                 driver->kind,
                                 netlist::DriverBitRange(driver->getBounds()),
                                 driver->getSourceRange().start());
    }

    items.emplace_back(std::move(value));
  }
};

} // namespace slang::report
