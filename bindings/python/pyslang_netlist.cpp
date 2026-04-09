#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"

#include "netlist/NetlistBuilder.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistNode.hpp"
#include "netlist/NetlistPath.hpp"
#include "netlist/PathFinder.hpp"
#include "netlist/ReportDrivers.hpp"
#include "netlist/ReportVariables.hpp"

#include <ranges>
#include <string>

using namespace slang;
namespace py = pybind11;

/// Helper to wrap FormatBuffer output as a string.
namespace {
auto reportDriversToString(slang::netlist::ReportDrivers &self) -> std::string {
  slang::FormatBuffer buffer;
  self.report(buffer);
  return buffer.str();
}
} // namespace

PYBIND11_MODULE(pyslang_netlist, m) {
  m.doc() = "Slang netlist";

  // Import pyslang to make all of Slang's python types available.
  py::module_ const pyslang = py::module_::import("pyslang");

  py::class_<netlist::ReportDrivers>(m, "ReportDrivers")
      .def(py::init<ast::Compilation &, analysis::AnalysisManager &>())
      .def("run",
           [&](netlist::ReportDrivers &self, ast::Compilation &compilation)
               -> void { compilation.getRoot().visit(self); })
      .def("report", &reportDriversToString, "Render driver info to a string");

  py::class_<netlist::NetlistGraph>(m, "NetlistGraph")
      .def(py::init<>())
      .def(
          "lookup",
          [](const netlist::NetlistGraph &self, std::string_view name) {
            netlist::NetlistNode const *node = self.lookup(name);
            return node ? py::cast(node) : py::none();
          },
          py::arg("name"), "Lookup a node by hierarchical name.")
      .def("num_nodes", &netlist::NetlistGraph::numNodes,
           "Get the number of nodes in the graph.")
      .def("num_edges", &netlist::NetlistGraph::numEdges,
           "Get the number of edges in the graph.")
      .def(
          "__iter__",
          [](netlist::NetlistGraph &self) {
            return py::make_iterator(self.begin(), self.end());
          },
          py::keep_alive<0, 1>(),
          "Return an iterator over the nodes in the graph.");

  py::class_<netlist::NetlistBuilder>(m, "NetlistBuilder")
      .def(py::init<ast::Compilation &, analysis::AnalysisManager &,
                    netlist::NetlistGraph &>())
      .def(
          "run",
          [&](netlist::NetlistBuilder &self, ast::Compilation &compilation,
              bool parallel, unsigned numThreads) -> void {
            // Match the CLI setup: fully materialize the lazy AST and freeze
            // the compilation before parallel netlist construction.
            netlist::VisitAll visitAll{};
            compilation.getRoot().visit(visitAll);
            compilation.freeze();
            self.build(compilation.getRoot(), parallel, numThreads);
          },
          py::arg("compilation"), py::arg("parallel") = true,
          py::arg("num_threads") = 0)
      .def("finalize", &netlist::NetlistBuilder::finalize);

  py::enum_<netlist::NodeKind>(m, "NodeKind")
      .value("None", netlist::NodeKind::None)
      .value("Port", netlist::NodeKind::Port)
      .value("Variable", netlist::NodeKind::Variable)
      .value("Assignment", netlist::NodeKind::Assignment)
      .value("Conditional", netlist::NodeKind::Conditional)
      .value("Case", netlist::NodeKind::Case)
      .value("Merge", netlist::NodeKind::Merge)
      .value("State", netlist::NodeKind::State);

  py::class_<netlist::NetlistNode>(m, "NetlistNode")
      .def_property_readonly(
          "ID", [](netlist::NetlistNode const &self) { return self.ID; })
      .def_property_readonly(
          "kind", [](netlist::NetlistNode const &self) { return self.kind; });

  py::class_<netlist::Port, netlist::NetlistNode>(m, "Port")
      .def_property_readonly(
          "name", [](netlist::Port const &self) { return self.name; })
      .def_property_readonly(
          "path",
          [](netlist::Port const &self) { return self.hierarchicalPath; })
      .def_property_readonly(
          "direction", [](netlist::Port const &self) { return self.direction; })
      .def_property_readonly(
          "bounds", [](netlist::Port const &self) { return self.bounds; })
      .def("is_input", &netlist::Port::isInput)
      .def("is_output", &netlist::Port::isOutput);

  py::class_<netlist::Variable, netlist::NetlistNode>(m, "Variable")
      .def_property_readonly(
          "name", [](netlist::Variable const &self) { return self.name; })
      .def_property_readonly(
          "path",
          [](netlist::Variable const &self) { return self.hierarchicalPath; })
      .def_property_readonly(
          "bounds", [](netlist::Variable const &self) { return self.bounds; });

  py::class_<netlist::State, netlist::NetlistNode>(m, "State")
      .def_property_readonly(
          "name", [](netlist::State const &self) { return self.name; })
      .def_property_readonly(
          "path",
          [](netlist::State const &self) { return self.hierarchicalPath; })
      .def_property_readonly(
          "bounds", [](netlist::State const &self) { return self.bounds; });

  py::class_<netlist::Assignment, netlist::NetlistNode>(m, "Assignment");

  py::class_<netlist::Conditional, netlist::NetlistNode>(m, "Conditional");

  py::class_<netlist::Case, netlist::NetlistNode>(m, "Case");

  py::class_<netlist::Merge, netlist::NetlistNode>(m, "Merge");

  py::class_<netlist::NetlistEdge>(m, "NetlistEdge")
      .def(py::init<netlist::NetlistNode &, netlist::NetlistNode &>())
      .def_property_readonly(
          "symbol_name",
          [](const netlist::NetlistEdge &self) { return self.symbol.name; })
      .def_property_readonly("symbol_path",
                             [](const netlist::NetlistEdge &self) {
                               return self.symbol.hierarchicalPath;
                             })
      .def_property_readonly(
          "bounds",
          [](const netlist::NetlistEdge &self) { return self.bounds; })
      .def_property_readonly("disabled", [](const netlist::NetlistEdge &self) {
        return self.disabled;
      });

  py::class_<netlist::NetlistPath>(m, "NetlistPath")
      .def(py::init<>())
      .def(py::init<netlist::NetlistPath::NodeListType>())
      .def("size", &netlist::NetlistPath::size)
      .def("empty", &netlist::NetlistPath::empty)
      .def("front", &netlist::NetlistPath::front,
           py::return_value_policy::reference)
      .def("back", &netlist::NetlistPath::back,
           py::return_value_policy::reference)
      .def(
          "__getitem__",
          [](const netlist::NetlistPath &self, size_t i) { return self[i]; },
          py::return_value_policy::reference)
      .def("__len__", &netlist::NetlistPath::size)
      .def(
          "__iter__",
          [](const netlist::NetlistPath &self) {
            return py::make_iterator(self.begin(), self.end());
          },
          py::keep_alive<0, 1>());

  py::class_<netlist::PathFinder>(m, "PathFinder")
      .def(py::init<>())
      .def("find", &netlist::PathFinder::find, py::arg("start_node"),
           py::arg("end_node"),
           "Find a path between two nodes in the netlist and return a "
           "NetlistPath.")
      .def("find_comb", &netlist::PathFinder::findComb, py::arg("start_node"),
           py::arg("end_node"),
           "Find a combinatorial path between two nodes that does not pass "
           "through State nodes. Return an empty NetlistPath if no "
           "combinatorial path exists.");
}
