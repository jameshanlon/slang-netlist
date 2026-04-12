#pragma once

#include "netlist/TextLocation.hpp"

#include "slang/analysis/ValueDriver.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

namespace slang::netlist {

struct Utilities {

  /// Return a string representation of a slang SourceLocation.
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

  /// Return a string representation of a TextLocation.
  static auto locationStr(TextLocation const &location,
                          FileTable const &fileTable) {
    return location.toString(fileTable);
  }

  /// Return a string representation of a value path for a driver of a symbol.
  static auto driverPathToString(const ast::ValueSymbol &symbol,
                                 const analysis::ValueDriver &driver) {
    ast::EvalContext evalContext(symbol);
    ast::ValuePath path(*driver.lsp, evalContext);
    return path.toString(evalContext);
  }

  /// Wildcard pattern matching.
  ///  * matches zero or more characters.
  ///  ? matches one character.
  static bool wildcardMatch(const char *text, const char *pattern) {
    while (*pattern != '\0') {
      if (*pattern == '*') {
        if (wildcardMatch(text, pattern + 1)) {
          return true;
        }
        if (*text != '\0' && wildcardMatch(text + 1, pattern)) {
          return true;
        }
        return false;
      } else if (*pattern == '?') {
        if (*text == '\0') {
          return false;
        }
        pattern++;
        text++;
      } else {
        if (*pattern != *text) {
          return false;
        }
        pattern++;
        text++;
      }
    }
    return *text == '\0' && *pattern == '\0';
  }

  using Row = std::vector<std::string>;
  using Table = std::vector<Row>;

  struct TableFormatConfig {

    // Spaces between columns
    size_t padding;

    TableFormatConfig() : padding(2) {}
  };

  /// Format a table of data into the given format buffer.
  static auto formatTable(FormatBuffer &buffer, const Row &header,
                          const Table &rows, TableFormatConfig cfg = {}) {

    const std::size_t cols = header.size();

    // Compute column widths: max of header and all rows for each column.
    std::vector<std::size_t> widths(cols);

    for (std::size_t col = 0; col < cols; ++col) {
      std::size_t max_width = header[col].size();
      for (const auto &row : rows) {
        if (col < row.size()) {
          max_width = std::max(max_width, row[col].size());
        }
      }
      widths[col] = max_width;
    }

    auto appendRow = [&](const Row &row) {
      for (std::size_t col = 0; col < cols; ++col) {
        std::string_view value;
        if (col < row.size()) {
          value = row[col];
        } else {
          value = "";
        }

        // Left-align in a field of `widths[col]`.
        buffer.format("{:<{}}", value, widths[col]);

        // Padding between columns (except after the last).
        if (col + 1 < cols && cfg.padding > 0) {
          buffer.format("{: >{}}", "", cfg.padding);
        }
      }
      buffer.format("\n");
    };

    appendRow(header);
    for (const auto &row : rows) {
      appendRow(row);
    }
  }
};

} // namespace slang::netlist
