#pragma once

#include "common/Utilities.hpp"
#include "common/Wildcard.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"
#include "slang/text/SourceLocation.h"

#include <string>
#include <string_view>
#include <vector>

namespace slang::report {

/// CRTP base for the report visitors. Owns the shared `Compilation&`
/// reference and the collected-item storage, and provides the table
/// and JSON rendering loops that delegate per-item formatting to the
/// derived class.
///
/// A derived class @c D with item type @c Info must provide:
///   * `auto tableHeader() const -> netlist::Utilities::Row;`
///   * `void appendItemRows(netlist::Utilities::Table&, Info const&) const;`
///   * `void emitJsonItem(JsonWriter&, Info const&) const;`
/// and the relevant `handle(...)` methods for the AST visitor. Each
/// `handle` should consult `nameMatches(symbol.name)` before recording
/// to honour any `--name` filters set by the caller.
template <typename Derived, typename Info>
class ReportVisitorBase
    : public ast::ASTVisitor<Derived, ast::VisitFlags::Expressions |
                                          ast::VisitFlags::Canonical> {
protected:
  ast::Compilation &compilation;
  std::vector<Info> items;
  std::vector<std::string> nameFilters;

  /// Format a source location using the compilation's SourceManager.
  auto locationStr(SourceLocation loc) const -> std::string {
    return netlist::Utilities::locationStr(compilation, loc);
  }

  /// Return true if no name filters are set, or if @p name matches at
  /// least one of them (glob syntax per netlist::wildcardMatch).
  auto nameMatches(std::string_view name) const -> bool {
    if (nameFilters.empty()) {
      return true;
    }
    auto subject = std::string(name);
    for (auto const &pattern : nameFilters) {
      if (netlist::wildcardMatch(subject.c_str(), pattern.c_str())) {
        return true;
      }
    }
    return false;
  }

public:
  explicit ReportVisitorBase(ast::Compilation &compilation)
      : compilation(compilation) {}

  /// Restrict reporting to symbols whose leaf name matches at least one
  /// of @p filters (glob syntax). An empty list disables filtering.
  void setNameFilters(std::vector<std::string> filters) {
    nameFilters = std::move(filters);
  }

  /// Render the collected information as a human-readable table.
  void report(FormatBuffer &buffer) {
    auto header = static_cast<Derived const *>(this)->tableHeader();
    auto table = netlist::Utilities::Table{};
    for (auto const &item : items) {
      static_cast<Derived const *>(this)->appendItemRows(table, item);
    }
    netlist::Utilities::formatTable(buffer, header, table);
  }

  /// Render the collected information as a JSON array of objects.
  void report(JsonWriter &writer) {
    writer.startArray();
    for (auto const &item : items) {
      static_cast<Derived const *>(this)->emitJsonItem(writer, item);
    }
    writer.endArray();
  }
};

} // namespace slang::report
