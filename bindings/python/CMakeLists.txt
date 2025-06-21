pybind11_add_module(
  py_slang_netlist
  MODULE
  py_slang_netlist.cpp
)

target_link_libraries(
  py_slang_netlist
  PUBLIC netlist slang::slang
)

target_include_directories(
  py_slang_netlist PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/python
)

