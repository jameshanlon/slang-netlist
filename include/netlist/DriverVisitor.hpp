#pragma once

#include "netlist/LSPUtilities.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/LSPUtilities.h"

namespace slang::netlist {

/// Visitor for printing symbol information in a human-readable format.
class DriverVisitor : public ast::ASTVisitor<DriverVisitor,
                                             /*VisitStatements=*/false,
                                             /*VisitExpressions=*/true,
                                             /*VisitBad=*/false,
                                             /*VisitCanonical=*/true> {
  ast::Compilation &compilation;
  analysis::AnalysisManager &analysisManager;
  FormatBuffer &buffer;

  /// Formats a source location as a string.
  auto locationStr(SourceLocation location) {
    if (location.buffer() != SourceLocation::NoLocation.buffer()) {
      auto filename = compilation.getSourceManager()->getFileName(location);
      auto line = compilation.getSourceManager()->getLineNumber(location);
      auto column = compilation.getSourceManager()->getColumnNumber(location);
      return fmt::format("{}:{}:{}", filename, line, column);
    }
    return std::string("unknown location");
  }

public:
  explicit DriverVisitor(ast::Compilation &compilation,
                         analysis::AnalysisManager &analysisManager,
                         FormatBuffer &buffer)
      : compilation(compilation), analysisManager(analysisManager),
        buffer(buffer) {}

  void handle(const ast::ValueSymbol &symbol) {
    buffer.append(fmt::format("Value name={} path={} location={}\n",
                              symbol.name, symbol.getHierarchicalPath(),
                              locationStr(symbol.location)));
    auto drivers = analysisManager.getDrivers(symbol);
    for (auto &[driver, bounds] : drivers) {
      buffer.append(fmt::format("  [{}:{}] driven by prefix={} from {} at {}\n",
                                bounds.first, bounds.second,
                                LSPUtilities::getLSPName(symbol, *driver),
                                toString(driver->kind),
                                locationStr(driver->getSourceRange().start())));
    }
  }
};

} // namespace slang::netlist
