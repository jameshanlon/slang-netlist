add_test(NAME ${test_name}_py
         COMMAND ${Python_EXECUTABLE} -m unittest discover -s
                 ${CMAKE_CURRENT_LIST_DIR} -p "test_*.py")

set_tests_properties(
  ${test_name}_py
  PROPERTIES
    ENVIRONMENT
    "PYTHONPATH=${CMAKE_CURRENT_LIST_DIR}:${CMAKE_BINARY_DIR}/bindings/python:${slang_BINARY_DIR}/lib"
)
