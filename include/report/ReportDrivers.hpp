#pragma once

#include "common/Utilities.hpp"
#include "netlist/DriverBitRange.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/ASTVisitor.h"

namespace slang::report {

/// Visitor for printing driver information in a human-readable format.
class ReportDrivers
    : public ast::ASTVisitor<ReportDrivers, ast::VisitFlags::Expressions |
                                                ast::VisitFlags::Canonical> {
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

  /// Renders the collected driver information to the given format buffer.
  void report(FormatBuffer &buffer) {
    auto header =
        netlist::Utilities::Row{"Value", "Range", "Driver", "Type", "Location"};
    auto table = netlist::Utilities::Table{};

    for (auto value : values) {
      auto loc = netlist::Utilities::locationStr(compilation, value.location);
      table.push_back(netlist::Utilities::Row{value.path, "", "", "", loc});

      for (auto &driver : value.drivers) {
        auto kind =
            driver.kind == analysis::DriverKind::Procedural ? "proc" : "cont";
        auto loc =
            netlist::Utilities::locationStr(compilation, driver.location);
        table.push_back(netlist::Utilities::Row{"↳", toString(driver.bounds),
                                                driver.prefix, kind, loc});
      }
    }

    netlist::Utilities::formatTable(buffer, header, table);
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
      value.drivers.emplace_back(
          netlist::Utilities::driverPathToString(symbol, *driver), driver->kind,
          netlist::DriverBitRange(driver->getBounds()),
          driver->getSourceRange().start());
    }

    values.emplace_back(std::move(value));
  }
};

} // namespace slang::report
