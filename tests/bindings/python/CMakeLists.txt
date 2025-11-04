set(PYTHON_TEST_ENV
    "PYTHONPATH=${CMAKE_CURRENT_LIST_DIR}:${CMAKE_BINARY_DIR}/bindings/python:${slang_BINARY_DIR}/lib"
)

foreach(test_name IN ITEMS test_netlistgraph test_reportdrivers)
  add_test(NAME ${test_name}_py COMMAND ${Python_EXECUTABLE} -m unittest
                                        ${test_name})
  set_tests_properties(${test_name}_py PROPERTIES ENVIRONMENT
                                                  "${PYTHON_TEST_ENV}")
endforeach()
