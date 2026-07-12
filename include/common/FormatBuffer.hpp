#pragma once

#include <fmt/format.h>

#include <iterator>
#include <string>
#include <string_view>

namespace slang::netlist {

/// A minimal string-building buffer built on fmt, used to accumulate
/// formatted text output.
class FormatBuffer {
public:
  /// Append a string.
  void append(std::string_view str) { buf.append(str); }

  /// Append a single character.
  void append(char ch) { buf.push_back(ch); }

  /// Append the result of formatting @p fmt with @p args.
  template <typename... Args>
  void format(fmt::format_string<Args...> fmt, Args &&...args) {
    fmt::format_to(std::back_inserter(buf), fmt, std::forward<Args>(args)...);
  }

  /// The number of characters buffered.
  auto size() const -> size_t { return buf.size(); }

  /// A pointer to the buffered characters.
  auto data() const -> const char * { return buf.data(); }

  /// Whether the buffer is empty.
  auto empty() const -> bool { return buf.empty(); }

  /// Discard the buffered contents.
  void clear() { buf.clear(); }

  /// A copy of the buffered contents as a string.
  auto str() const -> std::string { return buf; }

private:
  std::string buf;
};

} // namespace slang::netlist
