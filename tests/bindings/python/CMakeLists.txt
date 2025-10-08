add_test(
  NAME test_reportdrivers_py
  COMMAND ${Python_EXECUTABLE} -m unittest test_reportdrivers.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
