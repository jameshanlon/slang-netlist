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
      .def(pybind11::init(
               [](py::object py_compilation, py::object py_analysis_manager) {
                 // Extract C++ pointers from pyslang objects.
                 auto *compilation = py_compilation.attr("_get_cpp_obj")()
                                         .cast<slang::ast::Compilation *>();
                 auto *analysis_manager =
                     py_analysis_manager.attr("_get_cpp_obj")()
                         .cast<slang::analysis::AnalysisManager *>();
                 return new slang::netlist::ReportDrivers(*compilation,
                                                          *analysis_manager);
               }),
           py::arg("compilation"), py::arg("analysis_manager"))
      .def("report", &report_drivers_to_string,
           "Render driver info to a string");
}
