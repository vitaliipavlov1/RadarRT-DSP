# app/CMakeLists.txt
#
# The application executable. It is intentionally thin — a single main.cpp
# that constructs and runs radar::Application. All wiring and lifecycle logic lives
# in the Application class inside radar_core, so the executable carries no logic of
# its own and the orchestration stays unit-testable through the library.

add_executable(radar_app main.cpp)

# A distinct output name: the target keeps the radar_* convention while the produced
# binary is simply 'radarrt-dsp'.
set_target_properties(radar_app PROPERTIES OUTPUT_NAME radarrt-dsp)

# Sanitizer instrumentation propagates from radar_core (linked PUBLIC there), so the
# executable only needs the core library and the project warning policy.
target_link_libraries(radar_app
    PRIVATE
        radar::core
        radar::project_warnings)
