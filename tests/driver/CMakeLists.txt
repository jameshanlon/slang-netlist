add_test(
  NAME python-driver-tests
  COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/driver_tests.py
          $<TARGET_FILE:slang-netlist>
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
