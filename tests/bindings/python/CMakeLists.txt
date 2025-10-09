add_test(NAME test_reportdrivers_py COMMAND ${Python_EXECUTABLE} -m unittest
                                            test_reportdrivers)

set_tests_properties(
  test_reportdrivers_py
  PROPERTIES
    ENVIRONMENT
    "PYTHONPATH=${CMAKE_CURRENT_LIST_DIR}:${CMAKE_BINARY_DIR}/bindings/python:${slang_BINARY_DIR}/lib"
)
