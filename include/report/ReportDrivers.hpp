#pragma once

#include "common/Utilities.hpp"
#include "netlist/DriverBitRange.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/text/Json.h"

namespace slang::report {

/// Visitor for printing driver information.
class ReportDrivers
    : public ast::ASTVisitor<ReportDrivers, ast::VisitFlags::Expressions |
                                                ast::VisitFlags::Canonical> {

  /// Return a string representation of the LSP for a driver of a symbol.
  static auto driverPathToString(const ast::ValueSymbol &symbol,
                                 const analysis::ValueDriver &driver) {
    ast::EvalContext evalContext(symbol);
    return driver.path.toString(evalContext);
  }

  static auto driverKindStr(analysis::DriverKind kind) -> std::string_view {
    return kind == analysis::DriverKind::Procedural ? "proc" : "cont";
  }

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

  ast::Compilation &compilation;
  analysis::AnalysisManager &analysisManager;
  std::vector<ValueInfo> values;

public:
  explicit ReportDrivers(ast::Compilation &compilation,
                         analysis::AnalysisManager &analysisManager)
      : compilation(compilation), analysisManager(analysisManager) {}

  /// Render the collected driver information as a human-readable table.
  void report(FormatBuffer &buffer) {
    auto header =
        netlist::Utilities::Row{"Value", "Range", "Driver", "Type", "Location"};
    auto table = netlist::Utilities::Table{};

    for (auto value : values) {
      auto loc = netlist::Utilities::locationStr(compilation, value.location);
      table.push_back(netlist::Utilities::Row{value.path, "", "", "", loc});

      for (auto &driver : value.drivers) {
        auto loc =
            netlist::Utilities::locationStr(compilation, driver.location);
        table.push_back(netlist::Utilities::Row{
            "↳", toString(driver.bounds), driver.prefix,
            std::string(driverKindStr(driver.kind)), loc});
      }
    }

    netlist::Utilities::formatTable(buffer, header, table);
  }

  /// Render the collected driver information as JSON. Each value becomes
  /// an object with a nested `drivers` array.
  void report(JsonWriter &writer) {
    writer.startArray();
    for (auto const &value : values) {
      writer.startObject();
      writer.writeProperty("value");
      writer.writeValue(value.path);
      writer.writeProperty("location");
      writer.writeValue(
          netlist::Utilities::locationStr(compilation, value.location));
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
        writer.writeValue(
            netlist::Utilities::locationStr(compilation, driver.location));
        writer.endObject();
      }
      writer.endArray();
      writer.endObject();
    }
    writer.endArray();
  }

  /// Slang's AnalysisManager::getDrivers API returns all known drivers for
  /// static lvalue symbols (via the ValueSymbol type). Create a ValueInfo
  /// entry for each symbol and populate it with the driver information.
  void handle(ast::ValueSymbol const &symbol) {

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

    values.emplace_back(std::move(value));
  }
};

} // namespace slang::report
