add_executable(slang-report report.cpp)

target_include_directories(
  slang-report PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

target_link_libraries(slang-report PRIVATE slang::slang fmt::fmt)

install(TARGETS slang-report)
