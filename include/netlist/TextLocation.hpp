#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

namespace slang::netlist {

/// A centralised table of unique filenames, indexed by integer.  This avoids
/// duplicating filename strings across many TextLocation instances. Access
/// is thread-safe for concurrent insertion during parallel graph construction.
class FileTable {
  std::unordered_map<std::string, uint32_t> indexMap;
  std::vector<std::string_view> filenames;
  mutable std::mutex mutex;

public:
  static constexpr uint32_t NoFile = UINT32_MAX;

  /// Add a filename and return its index. If the filename already exists,
  /// returns the existing index. Thread-safe.
  auto addFile(std::string_view name) -> uint32_t {
    std::lock_guard lock(mutex);
    auto [it, inserted] = indexMap.try_emplace(
        std::string(name), static_cast<uint32_t>(filenames.size()));
    if (inserted) {
      filenames.push_back(it->first);
    }
    return it->second;
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
struct TextLocation {
  uint32_t fileIndex{FileTable::NoFile};
  size_t line{0};
  size_t column{0};

  TextLocation() = default;
  TextLocation(uint32_t fileIndex, size_t line, size_t column)
      : fileIndex(fileIndex), line(line), column(column) {}

  auto toString(FileTable const &fileTable) const -> std::string {
    if (fileIndex == FileTable::NoFile) {
      return "?";
    }
    return fmt::format("{}:{}:{}", fileTable.getFilename(fileIndex), line,
                       column);
  }

  auto empty() const -> bool { return fileIndex == FileTable::NoFile; }
};

} // namespace slang::netlist
