#pragma once

#include "common/Utilities.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"

namespace slang::report {

/// Visitor for printing port information.
class ReportPorts
    : public ast::ASTVisitor<ReportPorts, ast::VisitFlags::Expressions |
                                              ast::VisitFlags::Canonical> {
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

  /// Render the collected port information as a human-readable table.
  void report(FormatBuffer &buffer) {
    auto header = netlist::Utilities::Row{"Direction", "Name", "Location"};
    auto table = netlist::Utilities::Table{};
    for (auto port : ports) {
      auto loc = netlist::Utilities::locationStr(compilation, port.location);
      table.push_back(netlist::Utilities::Row{
          std::string(toString(port.direction)), port.name, loc});
    }
    netlist::Utilities::formatTable(buffer, header, table);
  }

  /// Render the collected port information as a JSON array of objects.
  void report(JsonWriter &writer) {
    writer.startArray();
    for (auto const &port : ports) {
      writer.startObject();
      writer.writeProperty("name");
      writer.writeValue(port.name);
      writer.writeProperty("direction");
      writer.writeValue(std::string_view(toString(port.direction)));
      writer.writeProperty("location");
      writer.writeValue(
          netlist::Utilities::locationStr(compilation, port.location));
      writer.endObject();
    }
    writer.endArray();
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

} // namespace slang::report
