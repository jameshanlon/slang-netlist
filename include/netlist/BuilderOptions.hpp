#pragma once

namespace slang::netlist {

/// Caller-supplied options that tune how the netlist graph is built.
struct BuilderOptions {
  /// When true, decompose concatenations, replications, conversions, and
  /// equal-width conditional operators so that bit-level dependencies
  /// between assignment LHS and RHS (and across port connections) are
  /// preserved. When false (default), behaviour matches earlier releases:
  /// each LSP on one side of an assignment fans into every LSP on the
  /// other side.
  bool resolveAssignBits = false;
};

} // namespace slang::netlist
