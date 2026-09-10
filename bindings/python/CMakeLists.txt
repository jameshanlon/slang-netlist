# Build in nanobind's split mode with the same backend module and stable ABI
# floor as pyslang, so both extensions share one nanobind runtime and can pass
# slang objects between each other.
nanobind_add_module(pyslang_netlist BACKEND_MODULE nanobind_backend
                    STABLE_ABI_VERSION 3.11 pyslang_netlist.cpp)

target_link_libraries(pyslang_netlist PUBLIC netlist slang::slang fmt::fmt)

target_include_directories(
  pyslang_netlist
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/python
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>
         $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

install(TARGETS pyslang_netlist DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES $<TARGET_FILE:pyslang> DESTINATION ${CMAKE_INSTALL_LIBDIR})
