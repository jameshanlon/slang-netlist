#pragma once

#include "common/Utilities.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/NetType.h"
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
    uint64_t width;
    std::string netType;
    SourceLocation location;
  };

  ast::Compilation &compilation;
  std::vector<PortInfo> ports;

  /// Return the net type name ("wire", "wand", ...) if the port connects
  /// to a NetSymbol, otherwise "var".
  static auto portNetTypeName(ast::PortSymbol const &symbol) -> std::string {
    if (auto const *net = symbol.internalSymbol
                              ? symbol.internalSymbol->as_if<ast::NetSymbol>()
                              : nullptr) {
      return std::string(net->netType.name);
    }
    return "var";
  }

public:
  explicit ReportPorts(ast::Compilation &compilation)
      : compilation(compilation) {}

  /// Render the collected port information as a human-readable table.
  void report(FormatBuffer &buffer) {
    auto header = netlist::Utilities::Row{"Direction", "Name", "Width",
                                          "Net Type", "Location"};
    auto table = netlist::Utilities::Table{};
    for (auto port : ports) {
      auto loc = netlist::Utilities::locationStr(compilation, port.location);
      table.push_back(netlist::Utilities::Row{
          std::string(toString(port.direction)), port.name,
          std::to_string(port.width), port.netType, loc});
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
      writer.writeProperty("width");
      writer.writeValue(port.width);
      writer.writeProperty("netType");
      writer.writeValue(port.netType);
      writer.writeProperty("location");
      writer.writeValue(
          netlist::Utilities::locationStr(compilation, port.location));
      writer.endObject();
    }
    writer.endArray();
  }

  void handle(const ast::PortSymbol &symbol) {
    ports.push_back(PortInfo{
        .name = symbol.getHierarchicalPath(),
        .direction = symbol.direction,
        .width = symbol.getType().getBitWidth(),
        .netType = portNetTypeName(symbol),
        .location = symbol.location,
    });
  }
};

} // namespace slang::report
