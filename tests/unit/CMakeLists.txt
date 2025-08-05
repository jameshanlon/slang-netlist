add_executable(
  netlist_unittests
  CycleDetectorTests.cpp
  DepthFirstSearchTests.cpp
  IntervalMapTests.cpp
  DirectedGraphTests.cpp
  NetlistTests.cpp
  PathTests.cpp
  Test.cpp)

target_link_libraries(netlist_unittests PRIVATE netlist slang::slang fmt::fmt
                                                Catch2::Catch2WithMain)

target_compile_definitions(netlist_unittests PRIVATE UNITTESTS)

target_include_directories(netlist_unittests PRIVATE ../include)

if(NETLIST_ENABLE_COVERAGE)
  target_coverage(netlist_unittests)
endif()

add_test(NAME netlist_unittests COMMAND netlist_unittests)
