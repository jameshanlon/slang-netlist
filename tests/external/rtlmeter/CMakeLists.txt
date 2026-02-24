find_package(
  Python
  COMPONENTS Interpreter
  REQUIRED)

cpmaddpackage(NAME rtlmeter GITHUB_REPOSITORY verilator/rtlmeter GIT_TAG main)

add_test(
  NAME rtlmeter-tests
  COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/rtlmeter_tests.py
          $<TARGET_FILE:slang-netlist> ${rtlmeter_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set_tests_properties(rtlmeter-tests PROPERTIES TIMEOUT 3600)
