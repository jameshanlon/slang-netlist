cmake_minimum_required(VERSION 3.15)

project(slang-netlist)

include(FetchContent)

FetchContent_Declare(
  slang
  GIT_REPOSITORY https://github.com/MikePopoloski/slang.git
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(slang)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG        e69e5f977d458f2650bb346dadf2ad30c5320281) # 10.2.1
FetchContent_MakeAvailable(fmt)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(slang-netlist main.cpp
                            source/Netlist.cpp)
target_include_directories(slang-netlist PUBLIC 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/include
)
target_link_libraries(slang-netlist PUBLIC slang::slang fmt::fmt)
