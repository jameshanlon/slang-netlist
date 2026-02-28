#pragma once

#include "netlist/FlatNetlistGraph.hpp"
#include "netlist/NetlistGraph.hpp"

#include <string>
#include <string_view>

namespace slang::netlist {

/// Serialise and deserialise the netlist graph to/from a JSON snapshot on
/// disk.
///
/// The JSON format captures the full graph topology plus the data that can be
/// extracted from each node and edge without access to the live slang AST:
/// node IDs, kinds, hierarchical paths, bit ranges, port directions, and edge
/// attributes.  Because slang AST references are not preserved, the
/// deserialised result is a FlatNetlistGraph rather than the original
/// NetlistGraph.
///
/// Format (version 1):
/// @code{.json}
/// {
///   "version": 1,
///   "nodes": [
///     { "id": 1, "kind": "Port", "path": "m.a", "name": "a",
///       "bounds": [0, 0], "direction": "In" },
///     { "id": 2, "kind": "Variable", "path": "m.b", "name": "b",
///       "bounds": [7, 0] },
///     { "id": 3, "kind": "Assignment" }
///   ],
///   "edges": [
///     { "source": 1, "target": 3, "edgeKind": "None",
///       "symbol": "a", "bounds": [0, 0], "disabled": false }
///   ]
/// }
/// @endcode
struct NetlistSerializer {
  static constexpr int formatVersion = 1;

  /// Serialise @p netlist to a pretty-printed JSON string.
  static auto serialize(NetlistGraph const &netlist) -> std::string;

  /// Deserialise a JSON string produced by serialize() into a FlatNetlistGraph.
  ///
  /// @throws std::runtime_error on parse failure or unsupported version.
  static auto deserialize(std::string_view json) -> FlatNetlistGraph;
};

} // namespace slang::netlist
