#pragma once

#include <string>

#include "slang/text/SourceLocation.h"

namespace slang::netlist {

/// Extracted identity of an AST symbol, decoupled from the live AST.
struct SymbolReference {
  std::string name;
  std::string hierarchicalPath;
  SourceLocation location;

  SymbolReference() = default;
  SymbolReference(std::string name, std::string hierarchicalPath,
                  SourceLocation location)
      : name(std::move(name)), hierarchicalPath(std::move(hierarchicalPath)),
        location(location) {}

  auto empty() const -> bool { return name.empty(); }
};

} // namespace slang::netlist
