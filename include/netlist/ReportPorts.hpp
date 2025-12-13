#pragma once

#include "netlist/Utilities.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/text/FormatBuffer.h"

namespace slang::netlist {

/// Visitor for printing port information in a human-readable format.
class ReportPorts : public ast::ASTVisitor<ReportPorts,
                                           /*VisitStatements=*/false,
                                           /*VisitExpressions=*/true,
                                           /*VisitBad=*/false,
                                           /*VisitCanonical=*/true> {
  struct PortInfo {
    std::string name;
    ast::ArgumentDirection direction;
    SourceLocation location;
  };

  ast::Compilation &compilation;
  std::vector<PortInfo> ports;

public:
  explicit ReportPorts(ast::Compilation &compilation)
      : compilation(compilation) {}

  /// Renders the collected variable information to the given format buffer.
  void report(FormatBuffer &buffer) {
    auto header = Utilities::Row{"Direction", "Name", "Location"};
    auto table = Utilities::Table{};
    for (auto port : ports) {
      auto loc = Utilities::locationStr(compilation, port.location);
      table.push_back(Utilities::Row{std::string(toString(port.direction)),
                                     port.name, loc});
    }
    Utilities::formatTable(buffer, header, table);
  }

  void handle(const ast::PortSymbol &symbol) {
    auto port = PortInfo{
        .name = symbol.getHierarchicalPath(),
        .direction = symbol.direction,
        .location = symbol.location,
    };
    ports.push_back(port);
  }
};

} // namespace slang::netlist
