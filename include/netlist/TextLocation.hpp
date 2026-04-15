#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "slang/text/SourceLocation.h"
#include "slang/util/ConcurrentMap.h"

namespace slang::netlist {

/// A centralised table of unique filenames, indexed by integer. This avoids
/// duplicating filename strings across many TextLocation instances. Lookups
/// are lock-free and only new insertions take a mutex.
class FileTable {
  // Keys are string_views into `filenames`, whose std::deque backing
  // guarantees stable addresses across insertions.
  concurrent_map<std::string_view, uint32_t> indexMap;
  std::deque<std::string> filenames;
  mutable std::mutex insertMutex;

public:
  static constexpr uint32_t NoFile = UINT32_MAX;

  /// Add a filename and return its index, or return the existing index if
  /// it is already present. Thread safe.
  auto addFile(std::string_view name) -> uint32_t {
    uint32_t result = NoFile;
    if (indexMap.visit(name, [&](auto const &kv) { result = kv.second; })) {
      return result;
    }
    std::lock_guard lock(insertMutex);
    if (indexMap.visit(name, [&](auto const &kv) { result = kv.second; })) {
      return result;
    }
    auto id = static_cast<uint32_t>(filenames.size());
    auto const &stored = filenames.emplace_back(name);
    indexMap.emplace(std::string_view(stored), id);
    return id;
  }

  /// Reserve capacity for the given number of entries.
  void reserve(size_t count) {
    std::lock_guard lock(insertMutex);
    indexMap.reserve(count);
  }

  /// Return the filename for the given index.
  auto getFilename(uint32_t index) const -> std::string_view {
    if (index == NoFile) {
      return {};
    }
    return filenames.at(index);
  }

  /// Return the number of unique filenames.
  auto size() const -> size_t { return filenames.size(); }
};

/// A serialisable source location, decoupled from the live slang AST.
/// Stores a file table index, line number, and column number.
///
/// Also carries a transient SourceLocation that is populated during graph
/// construction but not serialised. This allows pretty diagnostics (with
/// source lines and carets) when the compilation is still available.
struct TextLocation {
  uint32_t fileIndex{FileTable::NoFile};
  size_t line{0};
  size_t column{0};

  /// Transient — populated during construction, remains NoLocation after
  /// deserialisation. Not written to / read from JSON.
  SourceLocation sourceLocation{SourceLocation::NoLocation};

  TextLocation() = default;
  TextLocation(uint32_t fileIndex, size_t line, size_t column)
      : fileIndex(fileIndex), line(line), column(column) {}
  TextLocation(uint32_t fileIndex, size_t line, size_t column,
               SourceLocation sourceLocation)
      : fileIndex(fileIndex), line(line), column(column),
        sourceLocation(sourceLocation) {}

  auto toString(FileTable const &fileTable) const -> std::string {
    if (fileIndex == FileTable::NoFile) {
      return "?";
    }
    return fmt::format("{}:{}:{}", fileTable.getFilename(fileIndex), line,
                       column);
  }

  auto empty() const -> bool { return fileIndex == FileTable::NoFile; }

  auto hasSourceLocation() const -> bool {
    return sourceLocation != SourceLocation::NoLocation;
  }
};

} // namespace slang::netlist
