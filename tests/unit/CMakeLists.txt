add_executable(
  netlist_unittests
  BugTests.cpp
  CycleDetectorTests.cpp
  DepthFirstSearchTests.cpp
  ExternalManagerTests.cpp
  IntervalMapTests.cpp
  DirectedGraphTests.cpp
  NetlistTests.cpp
  InterfaceTests.cpp
  PortTests.cpp
  SequentialStateTests.cpp
  Test.cpp)

target_link_libraries(netlist_unittests PRIVATE netlist slang::slang fmt::fmt
                                                Catch2::Catch2WithMain)

target_compile_definitions(netlist_unittests PRIVATE UNITTESTS)

target_include_directories(netlist_unittests PRIVATE ../include)

if(CODE_COVERAGE)
  target_code_coverage(netlist_unittests AUTO)
endif()

add_test(NAME netlist_unittests COMMAND netlist_unittests)
