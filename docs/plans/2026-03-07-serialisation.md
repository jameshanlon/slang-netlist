# NetlistGraph Serialisation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Serialise and deserialise `NetlistGraph` to/from JSON, enabling save/load of netlists without a live slang compilation.

**Architecture:** Single `NetlistSerializer` class with static `serialize`/`deserialize` methods. JSON format version 2 includes `fileTable`, per-node `location`, and full `SymbolReference` on edges. `PathFinder` is decoupled from `NetlistBuilder` so it works on deserialised graphs.

**Tech Stack:** nlohmann/json for JSON parsing/generation, existing Catch2 for tests.

---

### Task 1: Add nlohmann/json dependency

**Files:**
- Modify: `CMakeLists.txt:21` (after the existing CPM packages)
- Modify: `source/CMakeLists.txt:8` (add to link libraries)

**Step 1: Add CPM package**

In `CMakeLists.txt`, after line 23 (`cpmaddpackage("gh:catchorg/Catch2@3.9.1")`), add:

```cmake
cpmaddpackage("gh:nlohmann/json@3.11.3")
```

**Step 2: Link the library**

In `source/CMakeLists.txt`, change line 8 from:

```cmake
target_link_libraries(netlist PRIVATE slang::slang fmt::fmt)
```

to:

```cmake
target_link_libraries(netlist PRIVATE slang::slang fmt::fmt nlohmann_json::nlohmann_json)
```

**Step 3: Verify the build**

Run: `cmake --build build/Local --target netlist 2>&1 | tail -5`
Expected: Build succeeds.

**Step 4: Commit**

```
Add nlohmann/json dependency for netlist serialisation
```

---

### Task 2: Remove NetlistBuilder dependency from PathFinder

**Files:**
- Modify: `include/netlist/PathFinder.hpp`
- Modify: `include/netlist/NetlistPath.hpp:3` (remove NetlistBuilder include)
- Modify: `tests/unit/Test.hpp:79`
- Modify: `tools/driver/driver.cpp:452`
- Modify: `bindings/python/pyslang_netlist.cpp:166-168`

**Step 1: Update PathFinder.hpp**

Remove the `NetlistBuilder const &` from `Visitor` and `PathFinder`:

```cpp
class Visitor {
public:
  Visitor(TraversalMap &traversalMap)
      : traversalMap(traversalMap) {}
  void visitedNode(NetlistNode &node) {}
  void visitNode(NetlistNode &node) {}
  void visitEdge(NetlistEdge &edge) {
    auto *sourceNode = &edge.getSourceNode();
    auto *targetNode = &edge.getTargetNode();
    SLANG_ASSERT(traversalMap.count(targetNode) == 0 &&
                 "node cannot have two parents");
    traversalMap[targetNode] = sourceNode;
  }
  void popNode() {}

private:
  TraversalMap &traversalMap;
};
```

Change the constructor and `find` method:

```cpp
public:
  PathFinder() = default;

  auto find(NetlistNode &startNode, NetlistNode &endNode) -> NetlistPath {
    TraversalMap traversalMap;
    Visitor visitor(traversalMap);
    DepthFirstSearch<NetlistNode, NetlistEdge, Visitor, EdgePredicate> dfs(
        visitor, startNode);
    return buildPath(traversalMap, startNode, endNode);
  }
```

Remove the private `NetlistBuilder const &netlist;` member.

**Step 2: Fix NetlistPath.hpp include**

Change line 3 from:
```cpp
#include "netlist/NetlistBuilder.hpp"
```
to:
```cpp
#include "netlist/NetlistNode.hpp"
```

(`NetlistPath` only needs `NetlistNode` for its `NodeListType`.)

**Step 3: Fix Test.hpp**

Change line 79 from:
```cpp
    PathFinder pathFinder(builder);
```
to:
```cpp
    PathFinder pathFinder;
```

**Step 4: Fix driver.cpp**

Change line 452 from:
```cpp
      PathFinder pathFinder(builder);
```
to:
```cpp
      PathFinder pathFinder;
```

**Step 5: Fix Python bindings**

In `bindings/python/pyslang_netlist.cpp`, change:
```cpp
  py::class_<netlist::PathFinder>(m, "PathFinder")
      .def(py::init<const netlist::NetlistBuilder &>())
```
to:
```cpp
  py::class_<netlist::PathFinder>(m, "PathFinder")
      .def(py::init<>())
```

**Step 6: Build and run tests**

Run: `cmake --build build/Local && ctest --test-dir build/Local`
Expected: All tests pass.

**Step 7: Commit**

```
Remove unused NetlistBuilder dependency from PathFinder
```

---

### Task 3: Create NetlistSerializer header

**Files:**
- Create: `include/netlist/NetlistSerializer.hpp`

**Step 1: Write the header**

```cpp
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
  /// The graph must be empty.  FileTable is populated from the JSON.
  ///
  /// @throws std::runtime_error on parse failure or unsupported version.
  static void deserialize(std::string_view json, NetlistGraph &graph);
};

} // namespace slang::netlist
```

**Step 2: Verify it compiles**

Run: `cmake --build build/Local --target netlist 2>&1 | tail -5`
Expected: Build succeeds (header only, no .cpp yet).

**Step 3: Commit**

```
Add NetlistSerializer header
```

---

### Task 4: Implement NetlistSerializer

**Files:**
- Create: `source/NetlistSerializer.cpp`
- Modify: `source/CMakeLists.txt:1` (add to source list)

**Step 1: Add to CMakeLists**

In `source/CMakeLists.txt`, change line 1 from:
```cmake
add_library(netlist NetlistNode.cpp NetlistBuilder.cpp NetlistGraph.cpp
                    DataFlowAnalysis.cpp ValueTracker.cpp)
```
to:
```cmake
add_library(netlist NetlistNode.cpp NetlistBuilder.cpp NetlistGraph.cpp
                    DataFlowAnalysis.cpp NetlistSerializer.cpp
                    ValueTracker.cpp)
```

**Step 2: Write the implementation**

Create `source/NetlistSerializer.cpp` with:
- Helper functions: `nodeKindToString`, `nodeKindFromString`, `edgeKindToString`, `edgeKindFromString`, `directionToString`, `directionFromString`
- `locationToJson(TextLocation)` → json object `{"fileIndex", "line", "column"}`
- `locationFromJson(json)` → `TextLocation`
- `serialize()`: iterate nodes (extract kind-specific fields), iterate edges (extract symbol/bounds/edgeKind/disabled), include fileTable
- `deserialize()`: parse fileTable, create nodes (polymorphic via `make_unique<Port>(...)` etc), build ID→node map, create edges

Key implementation details:
- `serialize` reads node data via `as<Port>()` etc based on `node->kind`
- `deserialize` creates the correct polymorphic subtype for each node kind
- FileTable is serialised as a JSON array of strings; on deserialise, each entry is added via `graph.fileTable.addFile()`
- TextLocation's `sourceLocation` field is NOT serialised (transient)
- Edges reference source/target by node ID; a `std::unordered_map<size_t, NetlistNode*>` maps IDs to nodes during deserialisation

**Step 3: Verify it compiles**

Run: `cmake --build build/Local --target netlist 2>&1 | tail -5`
Expected: Build succeeds.

**Step 4: Commit**

```
Implement NetlistSerializer serialize/deserialize
```

---

### Task 5: Write serialiser unit tests

**Files:**
- Create: `tests/unit/SerializerTests.cpp`
- Modify: `tests/unit/CMakeLists.txt:24` (add to source list)

**Step 1: Add to CMakeLists**

In `tests/unit/CMakeLists.txt`, add `SerializerTests.cpp` to the source list (after `SequentialStateTests.cpp`, before `Test.cpp`).

**Step 2: Write tests**

Create `tests/unit/SerializerTests.cpp` with these test cases (all tagged `[Serializer]`):

1. **Round-trip node and edge counts** — build netlist from simple SV (`assign b = a`), serialise, deserialise into fresh graph, check `numNodes()` and `numEdges()` match.

2. **FileTable preservation** — after round-trip, check `graph.fileTable.size()` matches and filenames are identical.

3. **TextLocation on nodes** — after round-trip, iterate Port nodes, check `location.fileIndex`, `location.line`, `location.column` match originals. Verify `hasSourceLocation()` returns false (transient field not serialised).

4. **TextLocation on edges** — after round-trip, find an edge with a non-empty symbol, check `symbol.location` fields match.

5. **Path finding on deserialised graph** — after round-trip, use `PathFinder` to find a path that existed in the original. Verify path is non-empty.

6. **Comb loop detection on deserialised graph** — use a comb-loop SV design, round-trip, run `CombLoops` on the deserialised graph, verify loops are found.

7. **Edge attributes** — after round-trip, verify `edgeKind`, `disabled`, `bounds` on edges match originals.

8. **Empty graph round-trip** — serialise an empty `NetlistGraph`, deserialise, verify `numNodes() == 0`.

9. **Version mismatch error** — manually craft JSON with `"version": 99`, attempt deserialise, verify `std::runtime_error` is thrown.

**Step 3: Build and run tests**

Run: `cmake --build build/Local --target netlist_unittests && ./build/Local/tests/unit/netlist_unittests "[Serializer]"`
Expected: All 9 tests pass.

**Step 4: Commit**

```
Add serialiser unit tests
```

---

### Task 6: Add --save-netlist and --load-netlist to driver

**Files:**
- Modify: `tools/driver/driver.cpp`

**Step 1: Add includes and CLI flags**

Add include at the top (after `NetlistGraph.hpp`):
```cpp
#include "netlist/NetlistSerializer.hpp"
```

Add CLI flag declarations (after `toPointName`):
```cpp
  std::optional<std::string> saveNetlistFile;
  driver.cmdLine.add(
      "--save-netlist", saveNetlistFile,
      "Save the netlist to a JSON file", "<file>",
      CommandLineFlags::FilePath);

  std::optional<std::string> loadNetlistFile;
  driver.cmdLine.add(
      "--load-netlist", loadNetlistFile,
      "Load a netlist from a JSON file (skips compilation)",
      "<file>", CommandLineFlags::FilePath);
```

**Step 2: Add --save-netlist handling**

After `builder.finalize()` and the debug print (line ~384), add:

```cpp
    if (saveNetlistFile) {
      auto json = NetlistSerializer::serialize(graph);
      OS::writeFile(*saveNetlistFile, json);
      return 0;
    }
```

**Step 3: Add --load-netlist handling**

Before the compilation block (before `driver.parseAllSources()`), add a branch for loading:

```cpp
    if (loadNetlistFile) {
      // Load netlist from JSON — no compilation needed.
      NetlistGraph graph;
      auto fileContent = OS::readFile(*loadNetlistFile);
      if (!fileContent) {
        SLANG_THROW(std::runtime_error(
            fmt::format("could not read file: {}", *loadNetlistFile)));
      }
      NetlistSerializer::deserialize(
          std::string_view(fileContent->data(), fileContent->size()),
          graph);

      // Handle analysis commands that work on a loaded netlist.
      // (reportRegisters, combLoops, netlistDotFile, path finding)
      // Duplicate the relevant command handling blocks but without
      // diagnostics (no compilation available).
    }
```

The load path supports: `--report-registers`, `--comb-loops`, `--netlist-dot`, `--from/--to` path finding. It does NOT support `--report-variables`, `--report-ports`, `--report-drivers` (these need the live AST).

For the loaded-netlist analysis section, reuse the existing blocks but pass `nullptr` for diagnostics in `reportPath` (triggers text fallback).

**Step 4: Build and run all tests**

Run: `cmake --build build/Local && ctest --test-dir build/Local`
Expected: All tests pass.

**Step 5: Commit**

```
Add --save-netlist and --load-netlist CLI flags
```

---

### Task 7: Run full test suite and verify

**Step 1: Build everything**

Run: `cmake --build build/Local`

**Step 2: Run all tests**

Run: `ctest --test-dir build/Local`
Expected: All tests pass.

**Step 3: Run clang-format**

Run: `git diff --name-only | xargs clang-format -i`

**Step 4: Final commit (if formatting changes)**

```
Format code
```
