#pragma once

#include "slang/ast/Compilation.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

#include <fmt/format.h>

namespace slang::netlist {

struct Utilities {

  /// Return a string representation of a source location.
  static auto locationStr(ast::Compilation const &compilation,
                          SourceLocation location) {
    if (location.buffer() != SourceLocation::NoLocation.buffer()) {
      auto filename = compilation.getSourceManager()->getFileName(location);
      auto line = compilation.getSourceManager()->getLineNumber(location);
      auto column = compilation.getSourceManager()->getColumnNumber(location);
      return fmt::format("{}:{}:{}", filename, line, column);
    }
    return std::string("?");
  }

  /// Return a string representation of an LSP for a driver for a symbol.
  static auto lspToString(const ast::ValueSymbol &symbol,
                          const analysis::ValueDriver &driver) {
    FormatBuffer buf;
    ast::EvalContext evalContext(symbol);
    ast::LSPUtilities::stringifyLSP(*driver.prefixExpression, evalContext, buf);
    return buf.str();
  }
};

} // namespace slang::netlist
