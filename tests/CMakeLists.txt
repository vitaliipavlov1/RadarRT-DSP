# tests/CMakeLists.txt
#
# Unit tests for the reusable infrastructure.
#
# Test-framework decision: a minimal in-repo assertion harness (tests/framework),
# NOT an external library. External test libraries are disallowed by default;
# GoogleTest/Catch2 via FetchContent would add a
# network dependency to every clean build and CI run for no benefit at this scope.
# The harness lives in tests/framework and is documented in README.md.
#
# All tests are compiled into a single executable, radar_tests. The executable
# takes an optional suite-name argument and runs only that suite, which lets CTest
# register one logical group at a time (see _radar_test_suites below) while the
# whole set still builds and links once.

add_executable(radar_tests
    framework/test_framework.cpp
    test_thread_safe_queue.cpp
    test_shutdown_token.cpp
    test_managed_thread.cpp
    test_heartbeat.cpp
    test_watchdog.cpp
    test_synchronization.cpp)

target_include_directories(radar_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/framework)

# radar::core carries the sanitizer instrumentation (linked PUBLIC there), so it
# propagates to the tests; the warning policy is applied directly so the test code
# is held to the same -Werror standard as the library.
target_link_libraries(radar_tests
    PRIVATE
        radar::core
        radar::project_warnings)

# One CTest entry per suite. Each invokes the binary filtered to that suite, so a
# failure points at the right group and `ctest` output stays granular. The suite
# names must match the first argument of the RADAR_TEST() macros in each source.
set(_radar_test_suites
    queue
    shutdown_token
    managed_thread
    heartbeat
    watchdog
    synchronization)

foreach(_suite IN LISTS _radar_test_suites)
    add_test(NAME ${_suite} COMMAND radar_tests ${_suite})
endforeach()
