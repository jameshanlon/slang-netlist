cmake_minimum_required(VERSION 3.20)

project(
  slang-netlist
  VERSION 0.4.0
  LANGUAGES CXX)

include(FetchContent)

option(BUILD_DOCS "Include documentation in the build" ON)
option(ENABLE_EXTERNAL_TESTS "Include tests using code from external sources"
       OFF)
option(ENABLE_PY_BINDINGS "Enable Python bindings" OFF)
option(CODE_COVERAGE "Enable code coverage" OFF)

# Required for slang to link with pyslang_netlist.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/CPM.cmake)

cpmaddpackage("gh:pybind/pybind11@3.0.1")
cpmaddpackage("gh:fmtlib/fmt#11.2.0")
cpmaddpackage("gh:catchorg/Catch2@3.9.1")
cpmaddpackage("gh:nlohmann/json@3.11.3")

if(ENABLE_PY_BINDINGS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
else()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

# Build slang with pyslang enabled.
cpmaddpackage(
  NAME
  slang
  GITHUB_REPOSITORY
  MikePopoloski/slang
  GIT_TAG
  74a5489606db1d7170836d6735e3dac712111994
  OPTIONS
  "SLANG_INCLUDE_PYLIB ${ENABLE_PY_BINDINGS}"
  EXCLUDE_FROM_ALL
  OFF)

if(BUILD_DOCS)
  cpmaddpackage("gh:mosra/m.css#0a460a7a9973a41db48f735e7b49e4da9a876325")
  add_subdirectory(docs)
endif()

if(USE_SANITIZER OR USE_STATIC_ANALYZER)
  cpmaddpackage("gh:StableCoder/cmake-scripts#24.04")
endif()

include(cmake/tools.cmake)

if(CODE_COVERAGE)

  if(NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "(Apple)?[Cc]lang")
    message(FATAL_ERROR "Coverage requires clang++ compiler")
  endif()

  # Find LLVM tools and LCOV
  find_program(LLVM_PROFDATA_PATH llvm-profdata)
  find_program(LLVM_COV_PATH llvm-cov)

  if(NOT LLVM_PROFDATA_PATH OR NOT LLVM_COV_PATH)
    message(FATAL_ERROR "Could not find llvm-profdata or llvm-cov")
  endif()

endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(source)
add_subdirectory(bindings)
add_subdirectory(tools)
enable_testing()
add_subdirectory(tests)

if(ENABLE_PY_BINDINGS)
  add_subdirectory(examples)
endif()

# Packaging

set(CPACK_PACKAGE_NAME "slang-netlist")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_FILENAME "${CPACK_PACKAGE_NAME}-${CMAKE_PROJECT_VERSION}")
include(CPack)
