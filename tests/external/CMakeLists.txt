FetchContent_Declare(
  picorv32 URL https://github.com/YosysHQ/picorv32/archive/refs/tags/v1.0.zip
               SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/picorv32)
FetchContent_MakeAvailable(picorv32)

add_test(NAME picorv32
         COMMAND slang-netlist ${CMAKE_CURRENT_BINARY_DIR}/picorv32/picorv32.v
                 --netlist-dot netlist.dot)
