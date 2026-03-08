#pragma once

#include <string>

#include "netlist/TextLocation.hpp"

namespace slang::netlist {

/// Extracted identity of an AST symbol, decoupled from the slang AST.
struct SymbolReference {
  std::string name;
  std::string hierarchicalPath;
  TextLocation location;

  SymbolReference() = default;
  SymbolReference(std::string name, std::string hierarchicalPath,
                  TextLocation location)
      : name(std::move(name)), hierarchicalPath(std::move(hierarchicalPath)),
        location(location) {}

  auto empty() const -> bool { return name.empty(); }
};

} // namespace slang::netlist
