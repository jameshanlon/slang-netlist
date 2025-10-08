pybind11_add_module(py_slang_netlist MODULE py_slang_netlist.cpp)

target_link_libraries(py_slang_netlist PUBLIC netlist slang::slang)

target_include_directories(
  py_slang_netlist
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/python
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>
         $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

install(TARGETS py_slang_netlist DESTINATION ${CMAKE_INSTALL_LIBDIR})
