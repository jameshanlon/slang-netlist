add_test(NAME example-connectivity-check
         COMMAND ${Python_EXECUTABLE}
                 ${CMAKE_CURRENT_LIST_DIR}/connectivity_check.py)

set_tests_properties(
  example-connectivity-check
  PROPERTIES
    ENVIRONMENT
    "PYTHONPATH=${CMAKE_BINARY_DIR}/bindings/python:${slang_BINARY_DIR}/lib")
