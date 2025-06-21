add_library(netlist Netlist.cpp)

target_include_directories(
  netlist
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>
         $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
)

target_link_libraries(netlist PRIVATE slang::slang fmt::fmt)

install(TARGETS netlist DESTINATION ${CMAKE_INSTALL_LIBDIR})
