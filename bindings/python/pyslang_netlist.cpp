#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"

#include "netlist/ReportDrivers.hpp"
#include "netlist/ReportingUtilities.hpp"

using namespace slang;
namespace py = pybind11;

/// Helper to wrap FormatBuffer output as a string.
namespace {
std::string report_drivers_to_string(slang::netlist::ReportDrivers &self) {
  slang::FormatBuffer buffer;
  self.report(buffer);
  return buffer.str();
}
} // namespace

PYBIND11_MODULE(pyslang_netlist, m) {
  m.doc() = "Slang netlist";

  // Import pyslang.
  py::module_ pyslang = py::module_::import("pyslang");

  py::class_<netlist::ReportDrivers>(m, "ReportDrivers")
      .def(py::init<ast::Compilation &, analysis::AnalysisManager &>())
      .def("run",
           [&](netlist::ReportDrivers &self, ast::Compilation &compilation) {
             compilation.getRoot().visit(self);
           })
      .def("report", &report_drivers_to_string,
           "Render driver info to a string");
}
