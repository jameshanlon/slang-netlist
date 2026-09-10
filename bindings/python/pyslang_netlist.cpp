#include <nanobind/make_iterator.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"

#include "netlist/BuilderOptions.hpp"
#include "netlist/DriverBitRange.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistNode.hpp"
#include "netlist/NetlistPath.hpp"
#include "netlist/PathFinder.hpp"
#include "netlist/VisitAll.hpp"

#include <string>
#include <vector>

using namespace slang;
namespace nb = nanobind;

namespace {

/// Recover the analysis manager underlying a Python
/// ``pyslang.analysis.AnalysisManager``.
///
/// pyslang binds a private wrapper around the manager rather than the manager
/// itself, so it cannot be accepted as an argument directly. The manager is
/// the first member of that wrapper, and so shares its address.
auto toAnalysisManager(nb::handle obj, nb::handle expectedType)
    -> analysis::AnalysisManager & {
  if (!nb::isinstance(obj, expectedType)) {
    throw nb::type_error("expected a pyslang.analysis.AnalysisManager");
  }
  return *nb::inst_ptr<analysis::AnalysisManager>(obj);
}

/// Iterator over the nodes of a graph, yielding node references rather than
/// the owning pointers the graph itself holds.
class NodeIterator {
public:
  NodeIterator(netlist::NetlistGraph const &graph, size_t index)
      : graph(&graph), index(index) {}

  auto operator*() const -> netlist::NetlistNode & {
    return graph->getNode(index);
  }

  auto operator++() -> NodeIterator & {
    ++index;
    return *this;
  }

  auto operator==(NodeIterator const &other) const -> bool {
    return index == other.index;
  }

private:
  netlist::NetlistGraph const *graph;
  size_t index;
};

} // namespace

NB_MODULE(pyslang_netlist, m) {
  m.doc() = "Slang netlist";

  // Import pyslang to make all of Slang's python types available.
  nb::module_ const pyslang = nb::module_::import_("pyslang");
  nb::object const analysisManagerType =
      pyslang.attr("analysis").attr("AnalysisManager");

  // ``DriverBitRange`` is returned from ``Port.bounds``, ``Variable.bounds``,
  // and ``NetlistEdge.bounds``. Deriving the binding from pyslang's
  // ``ConstantRange`` inherits its accessors, and works because both
  // extensions share one nanobind runtime.
  nb::class_<netlist::DriverBitRange, ConstantRange>(m, "DriverBitRange")
      .def(nb::init<int32_t, int32_t>(), nb::arg("lower"), nb::arg("upper"))
      .def(
          "__iter__",
          [](netlist::DriverBitRange const &self) {
            return nb::iter(nb::make_tuple(self.lower(), self.upper()));
          },
          "Iterate as (lower, upper) so callers can write "
          "`lo, hi = port.bounds`.")
      .def("__repr__", [](netlist::DriverBitRange const &self) {
        return netlist::toString(self);
      });

  nb::class_<netlist::VisitAll>(m, "VisitAll")
      .def(nb::init<>())
      .def(
          "run",
          [](netlist::VisitAll &self, ast::Compilation &compilation) {
            compilation.getRoot().visit(self);
          },
          nb::arg("compilation"),
          "Force construction of the whole AST by visiting every node. Must "
          "be called before freezing the compilation and building the "
          "netlist, since AST construction is lazy and visiting a previously "
          "unvisited node can mutate the compilation, which is not "
          "threadsafe.")
      .def_prop_ro(
          "count", [](netlist::VisitAll const &self) { return self.count; },
          "Number of value symbols visited.");

  nb::class_<netlist::NetlistGraph>(m, "NetlistGraph")
      .def(nb::init<>())
      .def(
          "lookup",
          [](const netlist::NetlistGraph &self, std::string_view name) {
            return self.lookup(name);
          },
          nb::arg("name"), nb::rv_policy::reference,
          "Lookup a node by hierarchical name.")
      .def(
          "lookup_by_range",
          [](const netlist::NetlistGraph &self, std::string_view name,
             int32_t lower, int32_t upper) {
            return self.lookup(name, netlist::DriverBitRange(lower, upper));
          },
          nb::arg("name"), nb::arg("lower"), nb::arg("upper"),
          nb::rv_policy::reference,
          "Lookup nodes by hierarchical name and bit range overlap.")
      .def("num_nodes", &netlist::NetlistGraph::numNodes,
           "Get the number of nodes in the graph.")
      .def("num_edges", &netlist::NetlistGraph::numEdges,
           "Get the number of edges in the graph.")
      .def(
          "__iter__",
          [](netlist::NetlistGraph &self) {
            return nb::make_iterator<nb::rv_policy::reference>(
                nb::type<netlist::NetlistGraph>(), "NetlistGraphIterator",
                NodeIterator(self, 0), NodeIterator(self, self.numNodes()));
          },
          nb::keep_alive<0, 1>(),
          "Return an iterator over the nodes in the graph.")
      .def(
          "build",
          [analysisManagerType](
              netlist::NetlistGraph &self, ast::Compilation &compilation,
              nb::handle analysisManager, bool parallel, unsigned numThreads,
              bool resolveAssignBits, bool propCutsAcrossPorts,
              std::vector<std::string> blackBoxes) {
            netlist::BuilderOptions const opts{
                .resolveAssignBits = resolveAssignBits,
                .propCutsAcrossPorts = propCutsAcrossPorts,
                .parallel = parallel,
                .numThreads = numThreads,
                .blackBoxes = std::move(blackBoxes)};
            self.build(compilation,
                       toAnalysisManager(analysisManager, analysisManagerType),
                       opts);
          },
          nb::arg("compilation"), nb::arg("analysis_manager"),
          nb::arg("parallel") = true, nb::arg("num_threads") = 0,
          nb::arg("resolve_assign_bits") = true,
          nb::arg("prop_cuts_across_ports") = true,
          nb::arg("black_boxes") = std::vector<std::string>{},
          "Build the netlist graph from an elaborated compilation. The "
          "caller is responsible for the full setup pipeline first: "
          "(1) run `VisitAll` to force lazy AST construction, "
          "(2) call `Compilation.freeze()`, "
          "(3) run `AnalysisManager.analyze()`, and "
          "(4) call `Compilation.unfreeze()` so the netlist builder can "
          "keep elaborating the AST. "
          "Set `resolve_assign_bits=False` to disable bit-aligned "
          "dependency resolution (on by default). "
          "Set `prop_cuts_across_ports=False` to disable propagation of "
          "concat-induced cut points across module port boundaries (on by "
          "default). "
          "Pass `black_boxes` as a list of glob patterns matched against "
          "each instance's definition name and hierarchical path; matched "
          "instances skip body traversal and record only port-boundary "
          "connectivity. Patterns support `*` (within a path segment), "
          "`**` or `...` (recursive across `.`), and `?` (single char "
          "within a segment).")
      .def(
          "get_drivers",
          [](const netlist::NetlistGraph &self, std::string_view name,
             int32_t lower, int32_t upper) {
            return self.getDrivers(name, netlist::DriverBitRange(lower, upper));
          },
          nb::arg("name"), nb::arg("lower"), nb::arg("upper"),
          nb::rv_policy::reference,
          "Return driver nodes for the symbol over the given bit range.")
      .def("get_comb_fan_out", &netlist::NetlistGraph::getCombFanOut,
           nb::arg("node"), nb::rv_policy::reference,
           "Return all nodes reachable via combinational edges in the "
           "forward direction. Stops at State nodes.")
      .def("get_comb_fan_in", &netlist::NetlistGraph::getCombFanIn,
           nb::arg("node"), nb::rv_policy::reference,
           "Return all nodes that can reach this node via combinational "
           "edges in the backward direction. Stops at State nodes.")
      .def("find_nodes", &netlist::NetlistGraph::findNodes, nb::arg("pattern"),
           nb::rv_policy::reference,
           "Find named nodes matching a glob pattern. Supports `*` "
           "(within a path segment), `**` or `...` (recursive across "
           "`.`), and `?` (single char within a segment).")
      .def("find_nodes_regex", &netlist::NetlistGraph::findNodesRegex,
           nb::arg("pattern"), nb::rv_policy::reference,
           "Find named nodes matching a regex pattern.")
      .def(
          "get_sensitivity",
          [](const netlist::NetlistGraph &self, netlist::NetlistNode &node) {
            nb::list result;
            for (auto const &s : self.getSensitivity(node)) {
              result.append(nb::make_tuple(
                  nb::cast(s.source, nb::rv_policy::reference), s.edgeKind));
            }
            return result;
          },
          nb::arg("node"),
          "Return the clocks gating the given node as a list of "
          "(source_node, edge_kind) tuples. For a State node, lists its own "
          "clocked in-edges; for any other node, the union of sensitivity "
          "over every State reachable by combinational fan-out. Deduplicated "
          "on (source, edge_kind). `edge_kind` is a `pyslang.ast.EdgeKind`.")
      .def("get_constant_drivers", &netlist::NetlistGraph::getConstantDrivers,
           nb::arg("node"), nb::rv_policy::reference,
           "Return the Constant nodes feeding `node` if its combinational "
           "fan-in bottoms out only at Constants (i.e. the sink is tied off "
           "to literal values). Returns an empty list if any non-constant "
           "source reaches `node` (a State node, or an undriven top-level "
           "input Port) or if `node` has no Constant in its fan-in.");

  nb::enum_<netlist::NodeKind>(m, "NodeKind")
      .value("None", netlist::NodeKind::None)
      .value("Port", netlist::NodeKind::Port)
      .value("Variable", netlist::NodeKind::Variable)
      .value("Assignment", netlist::NodeKind::Assignment)
      .value("Conditional", netlist::NodeKind::Conditional)
      .value("Case", netlist::NodeKind::Case)
      .value("Merge", netlist::NodeKind::Merge)
      .value("State", netlist::NodeKind::State)
      .value("Constant", netlist::NodeKind::Constant);

  nb::class_<netlist::NetlistNode>(m, "NetlistNode")
      .def_prop_ro("ID",
                   [](netlist::NetlistNode const &self) { return self.ID; })
      .def_prop_ro("kind",
                   [](netlist::NetlistNode const &self) { return self.kind; });

  nb::class_<netlist::Port, netlist::NetlistNode>(m, "Port")
      .def_prop_ro("name", [](netlist::Port const &self) { return self.name; })
      .def_prop_ro(
          "path",
          [](netlist::Port const &self) { return self.hierarchicalPath; })
      .def_prop_ro("direction",
                   [](netlist::Port const &self) { return self.direction; })
      .def_prop_ro("bounds",
                   [](netlist::Port const &self) { return self.bounds; })
      .def("is_input", &netlist::Port::isInput)
      .def("is_output", &netlist::Port::isOutput)
      .def("is_driven", &netlist::Port::isDriven,
           "Return True if any other node drives this port.");

  nb::class_<netlist::Variable, netlist::NetlistNode>(m, "Variable")
      .def_prop_ro("name",
                   [](netlist::Variable const &self) { return self.name; })
      .def_prop_ro(
          "path",
          [](netlist::Variable const &self) { return self.hierarchicalPath; })
      .def_prop_ro("bounds",
                   [](netlist::Variable const &self) { return self.bounds; });

  nb::class_<netlist::State, netlist::NetlistNode>(m, "State")
      .def_prop_ro("name", [](netlist::State const &self) { return self.name; })
      .def_prop_ro(
          "path",
          [](netlist::State const &self) { return self.hierarchicalPath; })
      .def_prop_ro("bounds",
                   [](netlist::State const &self) { return self.bounds; });

  nb::class_<netlist::Assignment, netlist::NetlistNode>(m, "Assignment");

  nb::class_<netlist::Conditional, netlist::NetlistNode>(m, "Conditional");

  nb::class_<netlist::Case, netlist::NetlistNode>(m, "Case");

  nb::class_<netlist::Merge, netlist::NetlistNode>(m, "Merge");

  nb::class_<netlist::Constant, netlist::NetlistNode>(m, "Constant")
      .def_prop_ro("width",
                   [](netlist::Constant const &self) { return self.width; })
      .def_prop_ro("value", [](netlist::Constant const &self) {
        return self.value.toString();
      });

  nb::class_<netlist::NetlistEdge>(m, "NetlistEdge")
      .def(nb::init<netlist::NetlistNode &, netlist::NetlistNode &>())
      .def_prop_ro("symbol_name",
                   [](const netlist::NetlistEdge &self) {
                     return self.symbol != nullptr ? self.symbol->name
                                                   : std::string{};
                   })
      .def_prop_ro("symbol_path",
                   [](const netlist::NetlistEdge &self) {
                     return self.symbol != nullptr
                                ? self.symbol->hierarchicalPath
                                : std::string{};
                   })
      .def_prop_ro("bounds",
                   [](const netlist::NetlistEdge &self) { return self.bounds; })
      .def_prop_ro("disabled", [](const netlist::NetlistEdge &self) {
        return self.disabled;
      });

  nb::class_<netlist::NetlistPath>(m, "NetlistPath")
      .def(nb::init<>())
      .def(nb::init<netlist::NetlistPath::NodeListType>())
      .def("size", &netlist::NetlistPath::size)
      .def("empty", &netlist::NetlistPath::empty)
      .def("front", &netlist::NetlistPath::front, nb::rv_policy::reference)
      .def("back", &netlist::NetlistPath::back, nb::rv_policy::reference)
      .def(
          "__getitem__",
          [](const netlist::NetlistPath &self, size_t i) { return self[i]; },
          nb::rv_policy::reference)
      .def("__len__", &netlist::NetlistPath::size)
      .def(
          "__iter__",
          [](const netlist::NetlistPath &self) {
            return nb::make_iterator<nb::rv_policy::reference>(
                nb::type<netlist::NetlistPath>(), "NetlistPathIterator",
                self.begin(), self.end());
          },
          nb::keep_alive<0, 1>());

  nb::class_<netlist::PathFinder>(m, "PathFinder")
      .def(nb::init<>())
      .def("find", &netlist::PathFinder::find, nb::arg("start_node"),
           nb::arg("end_node"),
           "Find a path between two nodes in the netlist and return a "
           "NetlistPath.")
      .def("find_comb", &netlist::PathFinder::findComb, nb::arg("start_node"),
           nb::arg("end_node"),
           "Find a combinatorial path between two nodes that does not pass "
           "through State nodes. Return an empty NetlistPath if no "
           "combinatorial path exists.");
}
