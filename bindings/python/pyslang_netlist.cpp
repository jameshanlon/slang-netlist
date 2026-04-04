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
#include <vector>

using namespace slang;
namespace py = pybind11;

/// Helper to wrap FormatBuffer output as a string.
namespace {
auto reportDriversToString(slang::netlist::ReportDrivers &self) -> std::string {
  slang::FormatBuffer buffer;
  self.report(buffer);
  return buffer.str();
}

auto rangeToTuple(slang::netlist::DriverBitRange const &range)
    -> std::pair<int32_t, int32_t> {
  return range.toPair();
}

auto directionToString(ast::ArgumentDirection direction) -> std::string_view {
  switch (direction) {
  case ast::ArgumentDirection::In:
    return "In";
  case ast::ArgumentDirection::Out:
    return "Out";
  case ast::ArgumentDirection::InOut:
    return "InOut";
  case ast::ArgumentDirection::Ref:
    return "Ref";
  }
  return "In";
}

auto edgeKindToString(ast::EdgeKind kind) -> std::string_view {
  switch (kind) {
  case ast::EdgeKind::None:
    return "None";
  case ast::EdgeKind::PosEdge:
    return "PosEdge";
  case ast::EdgeKind::NegEdge:
    return "NegEdge";
  case ast::EdgeKind::BothEdges:
    return "BothEdges";
  }
  return "None";
}

auto assignmentTypeToString(netlist::AssignmentType type) -> std::string_view {
  switch (type) {
  case netlist::AssignmentType::Continuous:
    return "assign";
  case netlist::AssignmentType::Initial:
    return "initial";
  case netlist::AssignmentType::Final:
    return "final";
  case netlist::AssignmentType::Always:
    return "always";
  case netlist::AssignmentType::AlwaysComb:
    return "always_comb";
  case netlist::AssignmentType::AlwaysLatch:
    return "always_latch";
  case netlist::AssignmentType::AlwaysFF:
    return "always_ff";
  case netlist::AssignmentType::Procedural:
    return "procedural";
  }
  return "procedural";
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
          [](const netlist::NetlistGraph &self,
             std::string_view name) -> py::object {
            netlist::NetlistNode const *node = self.lookup(name);
            if (node == nullptr) {
              return py::none();
            }
            return py::cast(node, py::return_value_policy::reference_internal,
                            py::cast(&self));
          },
          py::arg("name"), "Lookup a node by hierarchical name.")
      .def("num_nodes", &netlist::NetlistGraph::numNodes,
           "Get the number of nodes in the graph.")
      .def("num_edges", &netlist::NetlistGraph::numEdges,
           "Get the number of edges in the graph.")
      .def_property_readonly(
          "file_table",
          [](netlist::NetlistGraph &self) -> netlist::FileTable & {
            return self.fileTable;
          },
          py::return_value_policy::reference_internal)
      .def(
          "__iter__",
          [](netlist::NetlistGraph &self) {
            return py::make_iterator(self.begin(), self.end());
          },
          py::keep_alive<0, 1>(),
          "Return an iterator over the nodes in the graph.");

  py::class_<netlist::NetlistBuilder>(m, "NetlistBuilder")
      .def(py::init<ast::Compilation &, analysis::AnalysisManager &,
                    netlist::NetlistGraph &, bool>(),
           py::arg("compilation"), py::arg("analysis_manager"),
           py::arg("graph"), py::arg("materialize_internal_variables") = false)
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
          "kind", [](netlist::NetlistNode const &self) { return self.kind; })
      .def_property_readonly(
          "in_edges",
          [](netlist::NetlistNode const &self) {
            py::list result;
            for (auto const &edge : self.getInEdges()) {
              result.append(py::cast(
                  edge.get(), py::return_value_policy::reference_internal,
                  py::cast(&self)));
            }
            return result;
          })
      .def_property_readonly(
          "out_edges",
          [](netlist::NetlistNode const &self) {
            py::list result;
            for (auto const &edge : self.getOutEdges()) {
              result.append(py::cast(
                  edge.get(), py::return_value_policy::reference_internal,
                  py::cast(&self)));
            }
            return result;
          })
      .def_property_readonly(
          "in_degree",
          [](netlist::NetlistNode const &self) { return self.inDegree(); })
      .def_property_readonly(
          "out_degree",
          [](netlist::NetlistNode const &self) { return self.outDegree(); });

  py::class_<netlist::Port, netlist::NetlistNode>(m, "Port")
      .def_property_readonly(
          "name", [](netlist::Port const &self) { return self.name; })
      .def_property_readonly(
          "path",
          [](netlist::Port const &self) { return self.hierarchicalPath; })
      .def_property_readonly("direction",
                             [](netlist::Port const &self) {
                               return directionToString(self.direction);
                             })
      .def_property_readonly(
          "bounds",
          [](netlist::Port const &self) { return rangeToTuple(self.bounds); })
      .def_property_readonly(
          "location", [](netlist::Port const &self) { return self.location; })
      .def("is_input", &netlist::Port::isInput)
      .def("is_output", &netlist::Port::isOutput);

  py::class_<netlist::Variable, netlist::NetlistNode>(m, "Variable")
      .def_property_readonly(
          "name", [](netlist::Variable const &self) { return self.name; })
      .def_property_readonly(
          "path",
          [](netlist::Variable const &self) { return self.hierarchicalPath; })
      .def_property_readonly("bounds",
                             [](netlist::Variable const &self) {
                               return rangeToTuple(self.bounds);
                             })
      .def_property_readonly("location", [](netlist::Variable const &self) {
        return self.location;
      });

  py::class_<netlist::State, netlist::NetlistNode>(m, "State")
      .def_property_readonly(
          "name", [](netlist::State const &self) { return self.name; })
      .def_property_readonly(
          "path",
          [](netlist::State const &self) { return self.hierarchicalPath; })
      .def_property_readonly(
          "bounds",
          [](netlist::State const &self) { return rangeToTuple(self.bounds); })
      .def_property_readonly(
          "location", [](netlist::State const &self) { return self.location; });

  py::class_<netlist::Assignment, netlist::NetlistNode>(m, "Assignment")
      .def_property_readonly(
          "location",
          [](netlist::Assignment const &self) { return self.location; })
      .def_property_readonly("assignment_type",
                             [](netlist::Assignment const &self) {
                               return assignmentTypeToString(
                                   self.assignmentType);
                             })
      .def_property_readonly(
          "is_blocking",
          [](netlist::Assignment const &self) { return self.isBlocking; });

  py::class_<netlist::Conditional, netlist::NetlistNode>(m, "Conditional")
      .def_property_readonly("location", [](netlist::Conditional const &self) {
        return self.location;
      });

  py::class_<netlist::Case, netlist::NetlistNode>(m, "Case")
      .def_property_readonly(
          "location", [](netlist::Case const &self) { return self.location; });

  py::class_<netlist::Merge, netlist::NetlistNode>(m, "Merge");

  py::class_<netlist::NetlistEdge>(m, "NetlistEdge")
      .def(py::init<netlist::NetlistNode &, netlist::NetlistNode &>())
      .def_property_readonly(
          "source",
          [](const netlist::NetlistEdge &self) -> netlist::NetlistNode & {
            return self.getSourceNode();
          },
          py::return_value_policy::reference_internal)
      .def_property_readonly(
          "target",
          [](const netlist::NetlistEdge &self) -> netlist::NetlistNode & {
            return self.getTargetNode();
          },
          py::return_value_policy::reference_internal)
      .def_property_readonly(
          "symbol_name",
          [](const netlist::NetlistEdge &self) { return self.symbol.name; })
      .def_property_readonly("symbol_path",
                             [](const netlist::NetlistEdge &self) {
                               return self.symbol.hierarchicalPath;
                             })
      .def_property_readonly(
          "symbol_location",
          [](const netlist::NetlistEdge &self) { return self.symbol.location; })
      .def_property_readonly("bounds",
                             [](const netlist::NetlistEdge &self) {
                               return rangeToTuple(self.bounds);
                             })
      .def_property_readonly("edge_kind",
                             [](const netlist::NetlistEdge &self) {
                               return edgeKindToString(self.edgeKind);
                             })
      .def_property_readonly("disabled", [](const netlist::NetlistEdge &self) {
        return self.disabled;
      });

  py::class_<netlist::FileTable>(m, "FileTable")
      .def("get_filename", &netlist::FileTable::getFilename)
      .def("__len__", &netlist::FileTable::size);

  py::class_<netlist::TextLocation>(m, "TextLocation")
      .def_property_readonly(
          "file_index",
          [](netlist::TextLocation const &self) { return self.fileIndex; })
      .def_property_readonly(
          "line", [](netlist::TextLocation const &self) { return self.line; })
      .def_property_readonly(
          "column",
          [](netlist::TextLocation const &self) { return self.column; })
      .def("empty", &netlist::TextLocation::empty)
      .def("has_source_location", &netlist::TextLocation::hasSourceLocation)
      .def("to_string", &netlist::TextLocation::toString,
           py::arg("file_table"));

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
           "NetlistPath.");
}
