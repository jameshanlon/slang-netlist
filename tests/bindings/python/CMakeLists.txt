add_test(NAME pyslang_netlist_tests
         COMMAND ${Python_EXECUTABLE} -m unittest discover -s
                 ${CMAKE_CURRENT_LIST_DIR} -p "test_*.py")

set_tests_properties(
  pyslang_netlist_tests
  PROPERTIES
    ENVIRONMENT
    "PYTHONPATH=${CMAKE_SOURCE_DIR}/tests/utilities:${CMAKE_CURRENT_LIST_DIR}:${PY_BINDINGS_PATH}"
)
