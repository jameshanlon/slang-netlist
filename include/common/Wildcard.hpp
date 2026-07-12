#pragma once

#include <string_view>

namespace slang::netlist {

/// Glob-style pattern matching against hierarchical names whose path
/// segments are separated by `.`.
///
/// Pattern syntax:
///   `*`        zero or more characters within a single path segment
///              (does not match `.`).
///   `**`       zero or more characters across path boundaries (matches
///              `.`). Recursive equivalent of `*`.
///   `...`      same as `**`. Spelled this way for consistency with
///              the LRM-native form used elsewhere in slang.
///   `?`        exactly one character within a single path segment
///              (does not match `.`).
///   anything   matched literally.
///   else
///
/// When a recursive wildcard is adjacent to a literal `.` in the
/// pattern, that `.` is treated as an optional segment boundary, so
/// `a.**.b` matches `a.b`, `a.x.b`, and `a.x.y.b`. This mirrors the
/// gitignore-style `/**/` and slang's LRM-style `.../` handling, where
/// the recursive wildcard can stand in for zero or more intermediate
/// path segments.
inline auto wildcardMatch(const char *text, const char *pattern) -> bool {
  while (*pattern != '\0') {
    // Detect a recursive wildcard token at the current pattern
    // position, optionally preceded by a `.` we can absorb as part
    // of a segment-boundary match.
    bool hasLead = false;
    const char *p = pattern;
    if (*p == '.') {
      const char *q = p + 1;
      bool isRecur = (q[0] == '*' && q[1] == '*') ||
                     (q[0] == '.' && q[1] == '.' && q[2] == '.');
      if (isRecur) {
        hasLead = true;
        p = q;
      }
    }

    int recurLen = 0;
    if (p[0] == '*' && p[1] == '*') {
      recurLen = 2;
    } else if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
      recurLen = 3;
    }

    if (recurLen != 0) {
      const char *afterRecur = p + recurLen;
      bool hasTrail = (*afterRecur == '.');
      const char *rest = hasTrail ? afterRecur + 1 : afterRecur;

      if (hasLead && hasTrail) {
        // `.**.` — match a segment boundary `.` plus zero or more
        // additional `<chars>.` segments before the boundary.
        if (*text != '.') {
          return false;
        }
        ++text;
        if (wildcardMatch(text, rest)) {
          return true;
        }
        while (*text != '\0') {
          if (*text == '.' && wildcardMatch(text + 1, rest)) {
            return true;
          }
          ++text;
        }
        return false;
      }

      if (hasLead) {
        // `.**` with no trailing dot: match nothing, or `.` plus any
        // suffix (including further `.`s).
        if (wildcardMatch(text, rest)) {
          return true;
        }
        if (*text != '.') {
          return false;
        }
        ++text;
        while (true) {
          if (wildcardMatch(text, rest)) {
            return true;
          }
          if (*text == '\0') {
            return false;
          }
          ++text;
        }
      }

      if (hasTrail) {
        // `**.` with no leading dot: match nothing, or any prefix
        // ending at a `.` (which the trailing `.` absorbs).
        if (wildcardMatch(text, rest)) {
          return true;
        }
        while (*text != '\0') {
          if (*text == '.' && wildcardMatch(text + 1, rest)) {
            return true;
          }
          ++text;
        }
        return false;
      }

      // Standalone `**` / `...`: match any (possibly empty) chars.
      while (true) {
        if (wildcardMatch(text, rest)) {
          return true;
        }
        if (*text == '\0') {
          return false;
        }
        ++text;
      }
    }

    if (*pattern == '*') {
      // Single-segment wildcard: matches zero or more non-`.` chars.
      const char *rest = pattern + 1;
      while (true) {
        if (wildcardMatch(text, rest)) {
          return true;
        }
        if (*text == '\0' || *text == '.') {
          return false;
        }
        ++text;
      }
    }

    if (*pattern == '?') {
      // Single character within a segment; does not match `.` or end.
      if (*text == '\0' || *text == '.') {
        return false;
      }
      ++pattern;
      ++text;
      continue;
    }

    if (*pattern != *text) {
      return false;
    }
    ++pattern;
    ++text;
  }
  return *text == '\0';
}

/// Return true if @p path is @p scope itself or a descendant of it, treating
/// `.` as the hierarchy separator. So `top.cpu` contains `top.cpu` and
/// `top.cpu.alu.x`, but not `top.cpu2`. Matching is literal; use
/// wildcardMatch for glob patterns.
inline auto pathInScope(std::string_view path, std::string_view scope) -> bool {
  if (path.size() < scope.size() || path.substr(0, scope.size()) != scope) {
    return false;
  }
  return path.size() == scope.size() || path[scope.size()] == '.';
}

} // namespace slang::netlist
