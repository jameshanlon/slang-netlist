#include <pybind11/pybind11.h>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"

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
