add_executable(
  netlist_unittests
  BugTests.cpp
  ConditionalTests.cpp
  CycleDetectorTests.cpp
  DatatypeTests.cpp
  DepthFirstSearchTests.cpp
  DirectedGraphTests.cpp
  DriverTests.cpp
  ExternalManagerTests.cpp
  InstanceTests.cpp
  InterfaceTests.cpp
  IntervalMapTests.cpp
  LoopTests.cpp
  MiscTests.cpp
  NonblockingAssignmentTests.cpp
  PortTests.cpp
  SequentialStateTests.cpp
  Test.cpp)

target_link_libraries(netlist_unittests PRIVATE netlist slang::slang fmt::fmt
                                                Catch2::Catch2WithMain)

target_compile_definitions(netlist_unittests PRIVATE UNITTESTS)

target_include_directories(
  netlist_unittests PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>
                            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

if(CODE_COVERAGE)

  # Add coverage flags to the executable target
  target_compile_options(netlist_unittests PRIVATE -fprofile-instr-generate
                                                   -fcoverage-mapping)

  target_link_options(netlist_unittests PRIVATE -fprofile-instr-generate
                      -fcoverage-mapping)

  set(TEST_BINARY_PATH "$<TARGET_FILE:netlist_unittests>")
  set(PROFRAW_FILE "netlist_unittests.profraw")
  set(PROFDATA_FILE "netlist_unittests.profdata")
  set(LCOV_REPORT_FILE "coverage_report.lcov")
  set(COVERAGE_DIR "${CMAKE_BINARY_DIR}/coverage_output")
  set(COVERAGE_IGNORE_REGEX "")

  # Clean target.
  add_custom_target(
    coverage-clean
    COMMAND ${CMAKE_COMMAND} -E remove -f ${PROFRAW_FILE} ${PROFDATA_FILE}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${COVERAGE_DIR}
    COMMENT "Cleaning old coverage files")

  # Target to run the test and generate the raw profile data.
  add_custom_target(
    coverage-run
    COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${PROFRAW_FILE}
            ${TEST_BINARY_PATH}
    DEPENDS netlist_unittests coverage-clean
    COMMENT "Running tests with instrumentation")

  # Target to merge the raw profile data into a single profdata file.
  add_custom_target(
    coverage-merge
    COMMAND ${LLVM_PROFDATA_PATH} merge -sparse ${PROFRAW_FILE} -o
            ${PROFDATA_FILE}
    DEPENDS coverage-run
    COMMENT "Merging raw profile data")

  # Target to generate the LCOV report using llvm-cov.
  add_custom_target(
    coverage-lcov
    COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_DIR}
    COMMAND
      ${LLVM_COV_PATH} export ${TEST_BINARY_PATH}
      --instr-profile=${PROFDATA_FILE} --format=lcov
      --ignore-filename-regex=${COVERAGE_IGNORE_REGEX} >
      ${COVERAGE_DIR}/${LCOV_REPORT_FILE}
    DEPENDS coverage-merge
    COMMENT "Generating LCOV report in ${COVERAGE_DIR}/${LCOV_REPORT_FILE}")

endif()

add_test(NAME netlist_unittests COMMAND netlist_unittests)
