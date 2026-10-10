# Sanitizers.cmake
#
# Optional runtime sanitizers selected via the RADAR_SANITIZER cache variable,
# exposed as an INTERFACE target. Targets opt in by linking
# radar::project_sanitizers.
#
# Examples:
#   -DRADAR_SANITIZER=address;undefined
#   -DRADAR_SANITIZER=thread
#
# ThreadSanitizer is mutually exclusive with Address/UB sanitizers.

add_library(radar_project_sanitizers INTERFACE)
add_library(radar::project_sanitizers ALIAS radar_project_sanitizers)

set(RADAR_SANITIZER "" CACHE STRING
    "Sanitizers to enable: any of 'address', 'undefined', 'thread' (semicolon-separated)")

if(RADAR_SANITIZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers are only supported on GCC/Clang.")
    endif()

    set(_radar_san_list ${RADAR_SANITIZER})
    if("thread" IN_LIST _radar_san_list AND
       ("address" IN_LIST _radar_san_list OR "undefined" IN_LIST _radar_san_list))
        message(FATAL_ERROR
            "ThreadSanitizer cannot be combined with Address/UB sanitizers.")
    endif()

    set(_radar_san_flags "")
    foreach(_san IN LISTS _radar_san_list)
        if(_san STREQUAL "address")
            list(APPEND _radar_san_flags -fsanitize=address)
        elseif(_san STREQUAL "undefined")
            list(APPEND _radar_san_flags -fsanitize=undefined)
        elseif(_san STREQUAL "thread")
            list(APPEND _radar_san_flags -fsanitize=thread)
        else()
            message(FATAL_ERROR "Unknown sanitizer '${_san}'.")
        endif()
    endforeach()

    list(APPEND _radar_san_flags -fno-omit-frame-pointer -g)

    target_compile_options(radar_project_sanitizers INTERFACE ${_radar_san_flags})
    target_link_options(radar_project_sanitizers INTERFACE ${_radar_san_flags})

    message(STATUS "RadarRT-DSP sanitizers enabled: ${RADAR_SANITIZER}")
endif()
