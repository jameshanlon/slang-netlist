add_executable(slang-netlist driver.cpp)

target_include_directories(slang-netlist PUBLIC 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/include
)

target_link_libraries(slang-netlist PUBLIC netlist slang::slang fmt::fmt)
