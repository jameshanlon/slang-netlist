cmake_minimum_required(VERSION 3.15)

project(slang-netlist)

include(FetchContent)

option(NETLIST_BUILD_DOCS "Include documentation in the build" ON)

# Required for slang to link with py_slang_netlist.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

CPMAddPackage("gh:MikePopoloski/slang#4e3f41400b7e3deeb869dda77e822796cb08057f")
CPMAddPackage("gh:pybind11/pybind11@2.13.6")
CPMAddPackage("gh:fmtlib/fmt#10.2.1")
CPMAddPackage("gh:catchorg/Catch2@3.8.1")

if (NETLIST_BUILD_DOCS)
FetchContent_Declare(
  m.css
  GIT_REPOSITORY https://github.com/mosra/m.css 
  GIT_TAG 0a460a7a9973a41db48f735e7b49e4da9a876325
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(m.css)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(source)
add_subdirectory(bindings)
add_subdirectory(docs)
add_subdirectory(tools)
enable_testing()
add_subdirectory(tests)

