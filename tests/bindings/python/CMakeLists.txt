add_test(
  NAME test_reportdrivers_py
  COMMAND ${Python_EXECUTABLE} -m unittest test_reportdrivers.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

set_tests_properties(
  test_reportdrivers_py
  PROPERTIES
    ENVIRONMENT
    "PYTHONPATH=${CMAKE_BINARY_DIR}/bindings/python:${slang_BINARY_DIR}/lib")
