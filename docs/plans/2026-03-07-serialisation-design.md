# NetlistGraph Serialisation Design

## Context

NetlistGraph nodes now store `TextLocation` + strings instead of slang AST
references. This means we can serialise and deserialise `NetlistGraph` directly,
eliminating the `FlatNetlistGraph` intermediary used on the previous `serialiser`
branch.

## Approach

Single `NetlistSerializer` class with `serialize`/`deserialize` static methods.
Uses nlohmann/json. JSON format version 2.

## JSON Format (Version 2)

```json
{
  "version": 2,
  "fileTable": ["test.sv", "other.sv"],
  "nodes": [
    {"id": 1, "kind": "Port", "path": "m.a", "name": "a",
     "bounds": [0, 0], "direction": "In",
     "location": {"fileIndex": 0, "line": 2, "column": 31}},
    {"id": 3, "kind": "Assignment",
     "location": {"fileIndex": 0, "line": 4, "column": 10}}
  ],
  "edges": [
    {"source": 1, "target": 3, "edgeKind": "None",
     "symbol": {"name": "a", "path": "m.a",
                "location": {"fileIndex": 0, "line": 2, "column": 31}},
     "bounds": [0, 0], "disabled": false}
  ]
}
```

Changes from version 1:
- `fileTable` array (index = file ID used in locations)
- `location` objects on nodes and edge symbols
- Edge `symbol` is an object with name/path/location (was a string)

## Files

**New:**
- `include/netlist/NetlistSerializer.hpp`
- `source/NetlistSerializer.cpp`
- `tests/unit/SerializerTests.cpp`

**Modified:**
- `CMakeLists.txt` — add nlohmann/json@3.11.3 via CPM
- `source/CMakeLists.txt` — add NetlistSerializer.cpp, link nlohmann_json
- `tests/unit/CMakeLists.txt` — add SerializerTests.cpp
- `include/netlist/PathFinder.hpp` — remove unused NetlistBuilder dependency
- `tools/driver/driver.cpp` — add `--save-netlist` and `--load-netlist` flags

## Driver Behaviour

- `--save-netlist <file>`: serialise built netlist to JSON, exit
- `--load-netlist <file>`: skip compilation, deserialise, run analysis
- Mutually exclusive

## PathFinder Cleanup

`PathFinder::Visitor` stores a `NetlistBuilder const &` that is never used.
Remove this dependency so PathFinder works on deserialised graphs without a
builder.

## Tests

- Round-trip node/edge counts
- FileTable preservation
- TextLocation preservation on nodes and edges
- Path finding on deserialised graph
- Comb loop detection on deserialised graph
- Edge attributes (edgeKind, disabled, bounds) round-trip
- Empty graph round-trip
- Version mismatch error
