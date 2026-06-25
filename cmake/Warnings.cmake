# Warnings.cmake
#
# Centralized compiler-warning configuration exposed as an INTERFACE target.
# Library and test targets opt in by linking radar::project_warnings.
# Keeping the flags in one INTERFACE target avoids duplicating warning lists
# across every CMakeLists.txt and keeps the policy consistent.

add_library(radar_project_warnings INTERFACE)
add_library(radar::project_warnings ALIAS radar_project_warnings)

option(RADAR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

set(_radar_gcc_clang_warnings
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough)

if(RADAR_WARNINGS_AS_ERRORS)
    list(APPEND _radar_gcc_clang_warnings -Werror)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(radar_project_warnings INTERFACE ${_radar_gcc_clang_warnings})
else()
    message(WARNING
        "Unsupported compiler '${CMAKE_CXX_COMPILER_ID}'; project warning flags not applied.")
endif()
