cpmaddpackage(NAME rtlmeter GITHUB_REPOSITORY verilator/rtlmeter GIT_TAG main)

# Designs from the RTLMeter suite to include in the tests. Comment out or remove
# designs that are not yet passing.
set(RTLMETER_DESIGNS
    "BlackParrot"
    "Caliptra"
    "NVDLA"
    "OpenPiton"
    "OpenTitan"
    "VeeR-EH1"
    "VeeR-EH2"
    "VeeR-EL2"
    "Vortex"
    "XiangShan"
    "XuanTie-C906"
    "XuanTie-C910"
    "XuanTie-E902"
    "XuanTie-E906"
    "Example"
    "XiangShan")

add_test(
  NAME rtlmeter-tests
  COMMAND
    ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/rtlmeter_tests.py
    $<TARGET_FILE:slang-netlist> ${rtlmeter_SOURCE_DIR} ${RTLMETER_DESIGNS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set_tests_properties(rtlmeter-tests PROPERTIES TIMEOUT 3600)

add_custom_target(
  bench-threads
  COMMAND
    ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/rtlmeter_tests.py
    $<TARGET_FILE:slang-netlist> ${rtlmeter_SOURCE_DIR} ${RTLMETER_DESIGNS}
    --benchmark --threads 1 2 4 8
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS slang-netlist)
