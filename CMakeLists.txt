cmake_minimum_required(VERSION 3.20)

project(slang-netlist)

include(FetchContent)

option(NETLIST_BUILD_DOCS "Include documentation in the build" ON)
option(NETLIST_ENABLE_COVERAGE "Enable code coverage" OFF)

# Required for slang to link with py_slang_netlist.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/CPM.cmake)

cpmaddpackage("gh:MikePopoloski/slang#4e3f41400b7e3deeb869dda77e822796cb08057f")
cpmaddpackage("gh:pybind/pybind11@2.13.6")
cpmaddpackage("gh:fmtlib/fmt#10.2.1")
cpmaddpackage("gh:catchorg/Catch2@3.8.1")

if(NETLIST_BUILD_DOCS)
  cpmaddpackage("gh:mosra/m.css#0a460a7a9973a41db48f735e7b49e4da9a876325")
  add_subdirectory(docs)
endif()

if(NETLIST_ENABLE_COVERAGE
   OR USE_SANITIZER
   OR USE_STATIC_ANALYZER)
  cpmaddpackage("gh:StableCoder/cmake-scripts#24.04")
endif()

include(cmake/tools.cmake)

if(NETLIST_ENABLE_COVERAGE)
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
