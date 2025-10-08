#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "slang/text/FormatBuffer.h"

#include "netlist/ReportDrivers.hpp"
#include "netlist/ReportingUtilities.hpp"

namespace py = pybind11;

/// Helper to wrap FormatBuffer output as a string
namespace {
std::string report_drivers_to_string(slang::netlist::ReportDrivers &self) {
  slang::FormatBuffer buffer;
  self.report(buffer);
  return buffer.str();
}
} // namespace

PYBIND11_MODULE(py_slang_netlist, m) {
  m.doc() = "Slang netlist";

  // Import pyslang.
  // py::module_ pyslang = py::module_::import("pyslang");

  pybind11::class_<slang::netlist::ReportDrivers>(m, "ReportDrivers")
      .def(pybind11::init<slang::ast::Compilation &,
                          slang::analysis::AnalysisManager &>(),
           pybind11::arg("compilation"), pybind11::arg("analysisManager"))
      .def("report", &report_drivers_to_string,
           "Render driver info to a string");
}
