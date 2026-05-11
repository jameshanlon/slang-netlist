add_test(
  NAME python-report-tests
  COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/report_tests.py
          $<TARGET_FILE:slang-report>
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

set_tests_properties(
  python-report-tests
  PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}/tests/utilities")
