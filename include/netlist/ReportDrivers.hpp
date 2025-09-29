#pragma once

#include "netlist/LSPUtilities.hpp"
#include "netlist/ReportingUtilities.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/LSPUtilities.h"

namespace slang::netlist {

/// Visitor for printing symbol information in a human-readable format.
class ReportDrivers : public ast::ASTVisitor<ReportDrivers,
                                             /*VisitStatements=*/false,
                                             /*VisitExpressions=*/true,
                                             /*VisitBad=*/false,
                                             /*VisitCanonical=*/true> {
  struct DriverInfo {
    std::string prefix;
    analysis::DriverKind kind;
    DriverBitRange bounds;
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
    for (auto value : values) {
      auto loc = ReportingUtilities::locationStr(compilation, value.location);
      buffer.append(fmt::format("{:<60} {}\n", value.path, loc));
      for (auto &driver : value.drivers) {
        auto info = fmt::format(
            "  [{}:{}] by {} prefix={}", driver.bounds.first,
            driver.bounds.second,
            driver.kind == analysis::DriverKind::Procedural ? "proc" : "cont",
            driver.prefix);
        auto loc =
            ReportingUtilities::locationStr(compilation, driver.location);
        buffer.append(fmt::format("{:<60} {}\n", info, loc));
      }
    }
  }

  /// Slang's AnalysisManager::getDrivers API returns all known drivers for
  /// static lvalue symbols (via the ValueSymbol type). Create a ValueInfo
  /// entry for each symbol and populate it with the driver information.
  void handle(const ast::ValueSymbol &symbol) {

    auto value = ValueInfo{symbol.getHierarchicalPath(), symbol.location, {}};

    auto drivers = analysisManager.getDrivers(symbol);
    for (auto &[driver, bounds] : drivers) {
      value.drivers.emplace_back(LSPUtilities::getLSPName(symbol, *driver),
                                 driver->kind, bounds,
                                 driver->getSourceRange().start());
    }

    values.emplace_back(std::move(value));
  }
};

} // namespace slang::netlist
