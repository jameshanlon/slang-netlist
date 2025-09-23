#pragma once

#include "slang/ast/Compilation.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

#include <fmt/format.h>

namespace slang::netlist {

struct ReportingUtilities {

  /// Formats a source location as a string.
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
};

} // namespace slang::netlist
