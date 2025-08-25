#pragma once

#include "slang/ast/ASTVisitor.h"

namespace slang::netlist {

/// Visitor for printing symbol information in a human-readable format.
class SymbolVisitor : public ast::ASTVisitor<SymbolVisitor,
                                             /*VisitStatements=*/false,
                                             /*VisitExpressions=*/true,
                                             /*VisitBad=*/false,
                                             /*VisitCanonical=*/true> {
  ast::Compilation &compilation;
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
  explicit SymbolVisitor(ast::Compilation &compilation, FormatBuffer &buffer)
      : compilation(compilation), buffer(buffer) {}

  void handle(const ast::PortSymbol &symbol) {
    DEBUG_PRINT("PortSymbol {}\n", symbol.name);
    buffer.append(fmt::format("Port {} {} {}\n", symbol.name,
                              symbol.getHierarchicalPath(),
                              locationStr(symbol.location)));
  }

  void handle(const ast::ValueSymbol &symbol) {
    DEBUG_PRINT("ValueSymbol {}\n", symbol.name);
    buffer.append(fmt::format("Value {} {} {}\n", symbol.name,
                              symbol.getHierarchicalPath(),
                              locationStr(symbol.location)));
  }
};

} // namespace slang::netlist
