#pragma once

#include <cstring>
#include <fmt/format.h>
#include <source_location>

namespace slang::netlist {

/// Singleton holding global debug/quiet flags consulted by the
/// DEBUG_PRINT and INFO_PRINT macros.
class Config {
public:
  bool debugEnabled{false};
  bool quietEnabled{false};

  Config() = default;

  static auto getInstance() -> Config & {
    static Config instance;
    return instance;
  }

  Config(Config const &) = delete;
  void operator=(Config const &) = delete;
};

inline auto file_name(const char *file) -> const char * {
  return (strrchr(file, '/') != nullptr) ? strrchr(file, '/') + 1 : file;
}

/// Print a debug message with the current file and line number.
template <typename... T>
void DebugMessage(const std::source_location &location,
                  fmt::format_string<T...> fmt, T &&...args) {
  fmt::print("{}:{}: ", file_name(location.file_name()), location.line());
  fmt::print(fmt, std::forward<T>(args)...);
}

/// Print an informational message.
template <typename... T>
void InfoMessage(fmt::format_string<T...> fmt, T &&...args) {
  fmt::print(fmt, std::forward<T>(args)...);
}

} // namespace slang::netlist

#ifdef SLANG_DEBUG
#define DEBUG_PRINT(str, ...)                                                  \
  if (netlist::Config::getInstance().debugEnabled) {                           \
    DebugMessage(std::source_location::current(),                              \
                 str __VA_OPT__(, ) __VA_ARGS__);                              \
  }
#else
#define DEBUG_PRINT(str, ...)
#endif

#define INFO_PRINT(str, ...)                                                   \
  if (!Config::getInstance().quietEnabled) {                                   \
    InfoMessage(str __VA_OPT__(, ) __VA_ARGS__);                               \
  }
