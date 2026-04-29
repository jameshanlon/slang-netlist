#pragma once

namespace slang::netlist {

/// Caller-supplied options that tune how the netlist graph is built.
struct BuilderOptions {
  /// When true (default), decompose concatenations, replications,
  /// conversions, and equal-width conditional operators so that
  /// bit-level dependencies between assignment LHS and RHS (and across
  /// port connections) are preserved. When false, each LSP on one side
  /// of an assignment fans into every LSP on the other side, matching
  /// the behaviour of releases before bit-aligned resolution landed.
  bool resolveAssignBits = true;

  /// When true (default), propagate concat-induced cut points across
  /// module port boundaries so that paths through concatenated ports
  /// stay bit-precise. When false, port nodes and module-internal
  /// assignments are whole-word at port boundaries.
  bool propCutsAcrossPorts = true;

  /// When true, materialize an independent subgraph for every instance
  /// of a multi-instantiated module — including non-canonical instances
  /// that slang's analysis manager has folded onto a shared canonical
  /// body. When false (default), only the canonical body's connectivity
  /// is wired up; non-canonical instances appear as dangling nodes.
  bool resolveNonCanonicalInstances = false;
};

} // namespace slang::netlist
