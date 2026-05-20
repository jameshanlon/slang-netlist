#include "Test.hpp"

#include "Wildcard.hpp"

using slang::netlist::wildcardMatch;

TEST_CASE("wildcardMatch exact literal", "[Wildcard]") {
  CHECK(wildcardMatch("foo.bar", "foo.bar"));
  CHECK_FALSE(wildcardMatch("foo.bar", "foo.baz"));
  CHECK_FALSE(wildcardMatch("foo.bar", "foo"));
  CHECK_FALSE(wildcardMatch("foo", "foo.bar"));
  CHECK(wildcardMatch("", ""));
}

TEST_CASE("wildcardMatch '*' is single-segment", "[Wildcard]") {
  CHECK(wildcardMatch("foo", "*"));
  CHECK(wildcardMatch("foo.bar", "foo.*"));
  CHECK(wildcardMatch("foo.bar", "*.bar"));
  CHECK(wildcardMatch("foo.bar", "f*.b*"));

  // '*' must not cross '.'.
  CHECK_FALSE(wildcardMatch("foo.bar", "*"));
  CHECK_FALSE(wildcardMatch("foo.bar.baz", "foo.*"));
  CHECK_FALSE(wildcardMatch("foo.bar.baz", "*.baz"));
}

TEST_CASE("wildcardMatch '*' matches empty", "[Wildcard]") {
  CHECK(wildcardMatch("foo", "foo*"));
  CHECK(wildcardMatch("foo", "*foo"));
  CHECK(wildcardMatch("foo", "*foo*"));
  CHECK(wildcardMatch("", "*"));
}

TEST_CASE("wildcardMatch '**' crosses path boundaries", "[Wildcard]") {
  CHECK(wildcardMatch("foo.bar.baz", "**"));
  CHECK(wildcardMatch("foo.bar.baz", "foo.**"));
  CHECK(wildcardMatch("foo.bar.baz", "**.baz"));
  CHECK(wildcardMatch("foo.bar.baz", "foo.**.baz"));
  CHECK(wildcardMatch("foo.baz", "foo.**.baz"));
  CHECK(wildcardMatch("foo.x.y.z.baz", "foo.**.baz"));

  // Matches single-segment too.
  CHECK(wildcardMatch("foo", "**"));
  CHECK(wildcardMatch("", "**"));
}

TEST_CASE("wildcardMatch '**' segment-boundary zero-segment cases",
          "[Wildcard]") {
  // `foo.**` matches just `foo` (zero trailing segments).
  CHECK(wildcardMatch("foo", "foo.**"));
  CHECK(wildcardMatch("foo.x", "foo.**"));
  CHECK(wildcardMatch("foo.x.y", "foo.**"));
  // ...but not a prefix of `foo` like `fo` or `foox`.
  CHECK_FALSE(wildcardMatch("fo", "foo.**"));
  CHECK_FALSE(wildcardMatch("foox", "foo.**"));

  // `**.baz` matches just `baz`.
  CHECK(wildcardMatch("baz", "**.baz"));
  CHECK(wildcardMatch("x.baz", "**.baz"));
  CHECK(wildcardMatch("x.y.baz", "**.baz"));
  // The trailing `.` must be a real segment boundary.
  CHECK_FALSE(wildcardMatch("xbaz", "**.baz"));

  // The leading `.` of `.**.` must be a real segment boundary too.
  CHECK_FALSE(wildcardMatch("fooXbaz", "foo.**.baz"));
}

TEST_CASE("wildcardMatch '...' is equivalent to '**'", "[Wildcard]") {
  CHECK(wildcardMatch("foo.bar.baz", "..."));
  CHECK(wildcardMatch("foo.bar.baz", "foo..."));
  // `...baz` is standalone `...` + literal `baz` (no leading dot to
  // absorb); equivalent to `**baz`.
  CHECK(wildcardMatch("foo.bar.baz", "...baz"));
  // With the surrounding dots, `....baz` is `.` + `...` + `baz`;
  // when used as a suffix (`foo....baz`) the leading `.` of the
  // recursive token acts as a segment boundary.
  CHECK(wildcardMatch("foo.bar.baz", "foo....baz"));
  CHECK(wildcardMatch("foo.baz", "foo....baz"));
  CHECK(wildcardMatch("foo.x.y.z.baz", "foo....baz"));
  CHECK(wildcardMatch("", "..."));
}

TEST_CASE("wildcardMatch '?' is single non-'.' character", "[Wildcard]") {
  CHECK(wildcardMatch("a", "?"));
  CHECK(wildcardMatch("foo.a", "foo.?"));
  CHECK(wildcardMatch("foo.ab", "foo.??"));
  CHECK_FALSE(wildcardMatch("foo.ab", "foo.?"));
  CHECK_FALSE(wildcardMatch("", "?"));

  // '?' must not match '.'.
  CHECK_FALSE(wildcardMatch("a.b", "a?b"));
  CHECK_FALSE(wildcardMatch(".", "?"));
}

TEST_CASE("wildcardMatch combinations", "[Wildcard]") {
  // 'top.u_*' matches sibling instances with a u_ prefix.
  CHECK(wildcardMatch("top.u_a", "top.u_*"));
  CHECK(wildcardMatch("top.u_bar", "top.u_*"));
  // ...but not deeper instances.
  CHECK_FALSE(wildcardMatch("top.u_a.x", "top.u_*"));

  // '**' lets us cross.
  CHECK(wildcardMatch("top.u_a.x.y", "top.**.y"));

  // Mix '?' and '*'.
  CHECK(wildcardMatch("top.u_a", "top.u_?"));
  CHECK_FALSE(wildcardMatch("top.u_ab", "top.u_?"));
  CHECK(wildcardMatch("top.u_ab", "top.u_?*"));
}
