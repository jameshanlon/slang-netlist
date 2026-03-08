#pragma once

#include "netlist/NetlistGraph.hpp"

#include <string>
#include <string_view>

namespace slang::netlist {

/// Serialise and deserialise a NetlistGraph to/from JSON.
///
/// Format (version 2):
/// @code{.json}
/// {
///   "version": 2,
///   "fileTable": ["test.sv", "other.sv"],
///   "nodes": [
///     {"id": 1, "kind": "Port", "path": "m.a", "name": "a",
///      "bounds": [0, 0], "direction": "In",
///      "location": {"fileIndex": 0, "line": 2, "column": 31}}
///   ],
///   "edges": [
///     {"source": 1, "target": 3, "edgeKind": "None",
///      "symbol": {"name": "a", "path": "m.a",
///                 "location": {"fileIndex": 0, "line": 2, "column": 31}},
///      "bounds": [0, 0], "disabled": false}
///   ]
/// }
/// @endcode
struct NetlistSerializer {
  static constexpr int formatVersion = 2;

  /// Serialise @p graph to a pretty-printed JSON string.
  static auto serialize(NetlistGraph const &graph) -> std::string;

  /// Deserialise a JSON string into @p graph.
  /// The graph must be empty. FileTable is populated from the JSON.
  ///
  /// @throws std::runtime_error on parse failure or unsupported version.
  static void deserialize(std::string_view json, NetlistGraph &graph);
};

} // namespace slang::netlist
