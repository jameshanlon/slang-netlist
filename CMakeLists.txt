cmake_minimum_required(VERSION 3.20)

project(
  slang-netlist
  VERSION 0.0.0
  LANGUAGES CXX)

include(FetchContent)

option(NETLIST_BUILD_DOCS "Include documentation in the build" ON)
option(CODE_COVERAGE "Enable code coverage" OFF)

# Required for slang to link with pyslang_netlist.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/CPM.cmake)

cpmaddpackage("gh:pybind/pybind11@3.0.1")
cpmaddpackage("gh:fmtlib/fmt#11.2.0")
cpmaddpackage("gh:catchorg/Catch2@3.9.1")

# Build slang with pyslang enabled.
cpmaddpackage(
  NAME
  slang
  GITHUB_REPOSITORY
  MikePopoloski/slang
  GIT_TAG
  d9ac86a723653474548737f55384b0d99f447d92
  OPTIONS
  "SLANG_INCLUDE_PYLIB ON"
  EXCLUDE_FROM_ALL
  OFF)

if(NETLIST_BUILD_DOCS)
  cpmaddpackage("gh:mosra/m.css#0a460a7a9973a41db48f735e7b49e4da9a876325")
  add_subdirectory(docs)
endif()

if(CODE_COVERAGE
   OR USE_SANITIZER
   OR USE_STATIC_ANALYZER)
  cpmaddpackage("gh:StableCoder/cmake-scripts#24.04")
endif()

include(cmake/tools.cmake)

if(CODE_COVERAGE)
  include(${cmake-scripts_SOURCE_DIR}/code-coverage.cmake)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(source)
add_subdirectory(bindings)
add_subdirectory(tools)
enable_testing()
add_subdirectory(tests)

# Packaging

set(CPACK_PACKAGE_NAME "slang-netlist")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_FILENAME "${CPACK_PACKAGE_NAME}-${CMAKE_PROJECT_VERSION}")
include(CPack)
