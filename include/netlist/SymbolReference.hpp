#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "netlist/TextLocation.hpp"

#include "slang/util/ConcurrentMap.h"

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

/// Centralised table of unique SymbolReference records, keyed by
/// hierarchicalPath. Returned pointers are stable for the table's lifetime
/// and may be shared across many NetlistEdge instances. Lookups are
/// lock-free; insertion takes a mutex.
class SymbolTable {
  // Stable storage: std::deque guarantees addresses survive insertion.
  std::deque<SymbolReference> entries;
  // Keys are string_views into entries[i].hierarchicalPath.
  concurrent_map<std::string_view, SymbolReference const *> indexMap;
  mutable std::mutex insertMutex;

public:
  /// Intern a SymbolReference. Returns a stable pointer to the canonical
  /// record. Thread safe.
  auto intern(std::string_view name, std::string_view hierarchicalPath,
              TextLocation location) -> SymbolReference const * {
    SymbolReference const *result = nullptr;
    if (indexMap.visit(hierarchicalPath,
                       [&](auto const &kv) { result = kv.second; })) {
      return result;
    }
    std::lock_guard lock(insertMutex);
    if (indexMap.visit(hierarchicalPath,
                       [&](auto const &kv) { result = kv.second; })) {
      return result;
    }
    auto &stored = entries.emplace_back(
        std::string(name), std::string(hierarchicalPath), location);
    auto *ptr = &stored;
    indexMap.emplace(std::string_view(stored.hierarchicalPath), ptr);
    return ptr;
  }

  /// Convenience: intern by copying from an existing SymbolReference value.
  auto intern(SymbolReference const &ref) -> SymbolReference const * {
    return intern(ref.name, ref.hierarchicalPath, ref.location);
  }

  /// Number of unique symbol entries currently interned.
  auto size() const -> size_t { return entries.size(); }
};

} // namespace slang::netlist
