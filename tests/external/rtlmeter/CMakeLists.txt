cpmaddpackage(NAME rtlmeter GITHUB_REPOSITORY verilator/rtlmeter GIT_TAG main)

# Designs from the RTLMeter suite to include in the tests. Comment out or remove
# designs that are not yet passing.
#
# cmake-format: off

# Entry formats:
#   "DesignName"
#       Use the descriptor's 'default' configuration if one exists.
#   "DesignName:ConfigName"
#       Select a named configuration from the design's descriptor.yaml (e.g. to
#       pull in required preprocessor defines or extra source files).
#   "DesignName:ConfigName:--flag1 --flag2"
#       Also pass extra slang-netlist flags for this design.  Use the '=' form
#       for flags that take a value (e.g. '--top=mymodule').  Leave the config
#       segment empty to use the default: "DesignName:::--flag".

# "BlackParrot:1x1"
# "Caliptra"
# "NVDLA"
# "OpenPiton:1x1"
# "OpenTitan"
# "Vortex:mini"
# "XiangShan:mini-chisel6"
# "VeeR-EL2"
# "XuanTie-C906"
# "XuanTie-C910"
# "XuanTie-E902"
# "XuanTie-E906"

set(RTLMETER_DESIGNS
    "VeeR-EH1"
    "VeeR-EH2"
    "Example")

# cmake-format: on

add_test(
  NAME rtlmeter-tests
  COMMAND
    ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/rtlmeter_tests.py
    $<TARGET_FILE:slang-netlist> ${rtlmeter_SOURCE_DIR} ${RTLMETER_DESIGNS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set_tests_properties(rtlmeter-tests PROPERTIES TIMEOUT 3600)
