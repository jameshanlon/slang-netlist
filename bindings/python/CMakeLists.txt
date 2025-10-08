pybind11_add_module(pyslang_netlist MODULE pyslang_netlist.cpp)

target_link_libraries(pyslang_netlist PUBLIC netlist slang::slang)

target_include_directories(
  pyslang_netlist
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/python
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>
         $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

install(TARGETS pyslang_netlist DESTINATION ${CMAKE_INSTALL_LIBDIR})
