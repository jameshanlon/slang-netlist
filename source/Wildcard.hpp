#pragma once

namespace slang::netlist {

/// Wildcard pattern matching.
///  * matches zero or more characters.
///  ? matches one character.
inline bool wildcardMatch(const char *text, const char *pattern) {
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

} // namespace slang::netlist
