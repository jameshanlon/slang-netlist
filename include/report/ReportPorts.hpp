#pragma once

#include "report/ReportVisitorBase.hpp"

#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/NetType.h"

namespace slang::report {

struct PortInfo {
  std::string name;
  ast::ArgumentDirection direction;
  uint64_t width;
  std::string netType;
  SourceLocation location;
};

/// Visitor for printing port information.
class ReportPorts : public ReportVisitorBase<ReportPorts, PortInfo> {
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
  using ReportVisitorBase::ReportVisitorBase;

  auto tableHeader() const -> netlist::Utilities::Row {
    return {"Direction", "Name", "Width", "Net Type", "Location"};
  }

  void appendItemRows(netlist::Utilities::Table &table,
                      PortInfo const &port) const {
    table.push_back(netlist::Utilities::Row{
        std::string(toString(port.direction)), port.name,
        std::to_string(port.width), port.netType, locationStr(port.location)});
  }

  void emitJsonItem(JsonWriter &writer, PortInfo const &port) const {
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
    writer.writeValue(locationStr(port.location));
    writer.endObject();
  }

  void handle(const ast::PortSymbol &symbol) {
    auto path = symbol.getHierarchicalPath();
    if (!nameMatches(path)) {
      return;
    }
    items.push_back(PortInfo{
        .name = std::move(path),
        .direction = symbol.direction,
        .width = symbol.getType().getBitWidth(),
        .netType = portNetTypeName(symbol),
        .location = symbol.location,
    });
  }
};

} // namespace slang::report
