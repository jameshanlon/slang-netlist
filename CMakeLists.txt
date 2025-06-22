cmake_minimum_required(VERSION 3.15)

project(slang-netlist)

include(FetchContent)

# Required for slang to link with py_slang_netlist.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(
  slang
  GIT_REPOSITORY https://github.com/MikePopoloski/slang.git
  GIT_TAG 4e3f41400b7e3deeb869dda77e822796cb08057f
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(slang)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG        e69e5f977d458f2650bb346dadf2ad30c5320281) # 10.2.1
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.8.1
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11 
  GIT_TAG v2.13.6 
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(pybind11)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(source)
add_subdirectory(bindings)
add_subdirectory(tools)
enable_testing()
add_subdirectory(tests)

