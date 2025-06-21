cmake_minimum_required(VERSION 3.15)

project(slang-netlist)

include(FetchContent)

FetchContent_Declare(
  slang
  GIT_REPOSITORY git@github.com:jameshanlon/slang.git
  GIT_TAG constant-range-constructor 
  #GIT_REPOSITORY https://github.com/MikePopoloski/slang.git
  #GIT_TAG 21a690cee1eb8436cfd3b33f89c613371b6a4219
  GIT_SHALLOW ON
)
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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(source)
add_subdirectory(tools)
add_subdirectory(tests)

