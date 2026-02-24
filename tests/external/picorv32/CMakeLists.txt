FetchContent_Declare(
  picorv32 URL https://github.com/YosysHQ/picorv32/archive/refs/tags/v1.0.zip)
FetchContent_MakeAvailable(picorv32)

add_test(NAME picorv32 COMMAND slang-netlist ${picorv32_SOURCE_DIR}/picorv32.v
                               --netlist-dot netlist.dot)
