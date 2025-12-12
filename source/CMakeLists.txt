add_library(netlist NetlistNode.cpp NetlistBuilder.cpp NetlistGraph.cpp
                    DataFlowAnalysis.cpp ValueTracker.cpp)

target_include_directories(
  netlist PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                 $<INSTALL_INTERFACE:include>)

target_link_libraries(netlist PRIVATE slang::slang fmt::fmt)

if(CODE_COVERAGE)

  # Add coverage flags to the executable target
  target_compile_options(netlist PRIVATE -fprofile-instr-generate
                                         -fcoverage-mapping)

  target_link_options(netlist PRIVATE -fprofile-instr-generate
                      -fcoverage-mapping)

endif()

install(TARGETS netlist DESTINATION ${CMAKE_INSTALL_LIBDIR})
