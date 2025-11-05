#include <pybind11/pybind11.h>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"

#include "netlist/NetlistBuilder.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistNode.hpp"
#include "netlist/ReportDrivers.hpp"

#include <string>

using namespace slang;
namespace py = pybind11;

/// Helper to wrap FormatBuffer output as a string.
namespace {
auto report_drivers_to_string(slang::netlist::ReportDrivers &self)
    -> std::string {
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
      .def("report", &report_drivers_to_string,
           "Render driver info to a string");

  py::class_<netlist::NetlistGraph>(m, "NetlistGraph")
      .def(py::init<>())
      .def(
          "lookup",
          [](const netlist::NetlistGraph &self, std::string_view name) {
            auto const *node = self.lookup(name);
            return node ? py::cast(node) : py::none();
          },
          py::arg("name"), "Lookup a node by hierarchical name.");

  py::class_<netlist::NetlistBuilder>(m, "NetlistBuilder")
      .def(py::init<ast::Compilation &, analysis::AnalysisManager &,
                    netlist::NetlistGraph &>())
      .def("run",
           [&](netlist::NetlistBuilder &self, ast::Compilation &compilation)
               -> void { compilation.getRoot().visit(self); })
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
          "symbol", [](netlist::Port const &self) { return &self.symbol; })
      .def_property_readonly(
          "bounds", [](netlist::Port const &self) { return self.bounds; })
      .def("is_input", &netlist::Port::isInput)
      .def("is_output", &netlist::Port::isOutput);

  py::class_<netlist::Variable, netlist::NetlistNode>(m, "Variable")
      .def_property_readonly(
          "symbol", [](netlist::Variable const &self) { return &self.symbol; })
      .def_property_readonly(
          "bounds", [](netlist::Variable const &self) { return self.bounds; });

  py::class_<netlist::State, netlist::NetlistNode>(m, "State")
      .def_property_readonly(
          "symbol", [](netlist::State const &self) { return &self.symbol; })
      .def_property_readonly(
          "bounds", [](netlist::State const &self) { return self.bounds; });

  py::class_<netlist::Assignment, netlist::NetlistNode>(m, "Assignment")
      .def_property_readonly(
          "expr", [](netlist::Assignment const &self) { return &self.expr; });

  py::class_<netlist::Conditional, netlist::NetlistNode>(m, "Conditional")
      .def_property_readonly(
          "stmt", [](netlist::Conditional const &self) { return &self.stmt; });

  py::class_<netlist::Case, netlist::NetlistNode>(m, "Case")
      .def_property_readonly(
          "stmt", [](netlist::Case const &self) { return &self.stmt; });

  py::class_<netlist::Merge, netlist::NetlistNode>(m, "Merge");
}
