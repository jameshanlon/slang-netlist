add_subdirectory(bindings)
add_subdirectory(driver)
add_subdirectory(unit)

if(ENABLE_EXTERNAL_TESTS)
  add_subdirectory(external)
endif()
