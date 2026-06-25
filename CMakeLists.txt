cmake_minimum_required(VERSION 3.21)

project(RadarRT-DSP
    VERSION 0.1.0
    DESCRIPTION "Embedded real-time radar processing system (simulated)"
    LANGUAGES CXX)

# --- Guards -----------------------------------------------------------------

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. Configure into a separate build "
        "directory, e.g. 'cmake --preset debug'.")
endif()

# Single-config generators default to Debug for day-to-day development.
get_property(_radar_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _radar_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# --- Language standard ------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies -----------------------------------------------------------

# POSIX threads are required by the realtime/threading subsystems.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# --- Project-wide build configuration ---------------------------------------

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

option(RADAR_BUILD_TESTS "Build unit tests" ON)

# --- Targets ----------------------------------------------------------------

add_subdirectory(src)
add_subdirectory(app)

if(RADAR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "RadarRT-DSP ${PROJECT_VERSION} configured "
    "(build type: ${CMAKE_BUILD_TYPE}, tests: ${RADAR_BUILD_TESTS})")
