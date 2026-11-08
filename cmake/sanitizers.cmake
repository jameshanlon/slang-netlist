# Enables sanitizer support using the `USE_SANITIZER` flag. Its value is a
# comma- or semicolon-separated list of sanitizer names, for example
# `address,leak,undefined`.
#
# This must be included before any dependency is declared: the flags are applied
# as directory-scoped compile and link options, so they only reach targets
# created afterwards. Instrumenting the dependencies too keeps a single
# instrumentation regime across the whole binary.

if(USE_SANITIZER)
  include(${cmake-scripts_SOURCE_DIR}/sanitizers.cmake)

  string(REPLACE "," ";" SANITIZER_NAMES "${USE_SANITIZER}")
  string(TOLOWER "${SANITIZER_NAMES}" SANITIZER_NAMES)

  # Keep frame pointers so that reports carry usable stack traces in builds that
  # would otherwise omit them.
  if("address" IN_LIST SANITIZER_NAMES)
    set_sanitizer_options(address DEFAULT -fno-omit-frame-pointer)
  endif()

  # Abort on a report rather than the default of printing and continuing, so
  # that a finding fails the test run instead of only appearing in its output.
  if("undefined" IN_LIST SANITIZER_NAMES)
    set_sanitizer_options(undefined DEFAULT -fno-sanitize-recover=undefined)
  endif()

  add_sanitizer_support(${SANITIZER_NAMES})

  # Fail loudly rather than silently producing an uninstrumented build, which is
  # how a request for sanitizers went unnoticed once upstream dropped the
  # interface this relies on.
  get_directory_property(SANITIZER_APPLIED_OPTIONS COMPILE_OPTIONS)
  if(NOT SANITIZER_APPLIED_OPTIONS MATCHES "-fsanitize=")
    message(FATAL_ERROR "USE_SANITIZER=${USE_SANITIZER} applied no flags")
  endif()
endif()
