# Enables static analysis during configuration by passing
# `-DUSE_STATIC_ANALYZER=<VALUE>` to CMake, where the value is a comma- or
# semicolon-separated list of analyser names: clang-tidy, iwyu, cppcheck.
#
# This must be included after the dependencies are declared, so that only this
# project's own targets are analysed.

if(USE_STATIC_ANALYZER)
  include(${cmake-scripts_SOURCE_DIR}/tools.cmake)

  string(REPLACE "," ";" ANALYZER_NAMES "${USE_STATIC_ANALYZER}")
  string(TOLOWER "${ANALYZER_NAMES}" ANALYZER_NAMES)

  if("clang-tidy" IN_LIST ANALYZER_NAMES)
    clang_tidy(${CLANG_TIDY_ARGS})
  endif()

  if("iwyu" IN_LIST ANALYZER_NAMES)
    include_what_you_use(${IWYU_ARGS})
  endif()

  if("cppcheck" IN_LIST ANALYZER_NAMES)
    cppcheck(${CPPCHECK_ARGS})
  endif()

  # The helpers above record each analyser launcher in the cache, where a
  # reconfigure would pick it up ahead of the dependencies and analyse those
  # too. Re-scope them to this directory and below.
  foreach(LANG C CXX)
    foreach(TOOL CLANG_TIDY INCLUDE_WHAT_YOU_USE CPPCHECK)
      if(CMAKE_${LANG}_${TOOL})
        set(ANALYZER_LAUNCHER "${CMAKE_${LANG}_${TOOL}}")
        unset(CMAKE_${LANG}_${TOOL} CACHE)
        set(CMAKE_${LANG}_${TOOL} "${ANALYZER_LAUNCHER}")
      endif()
    endforeach()
  endforeach()
endif()
