# src/CMakeLists.txt
#
# Core static library: the reusable infrastructure (Config, Exceptions, Clock, Random,
# ShutdownToken, ManagedThread RAII primitive, ThreadSafeQueue<T>), the core data model
# (RadarSignal, ProcessedSignal, LogRecord), the realtime infrastructure (RealtimeManager,
# PiMutex), the health-monitoring primitives (Heartbeat, HeartbeatRegistry), the radar
# source (ISignalSource, RadarProducer), the DSP pipeline (SignalProcessor + NoiseFilter,
# FFT, TargetDetection), the DSP consumer pool (ThreadPool), asynchronous logging
# (ILogSink, ConsoleSink, FileSink, Logger), the health supervisor (Watchdog),
# observability (Metrics), async-signal-safe shutdown (SignalHandler), the composition
# root (Application; the executable lives in app/), and diagnostics.cpp: best-effort
# stderr reporting for the per-thread realtime-degradation warning and the D7 exception
# boundary, where the async Logger cannot be relied upon.

add_library(radar_core STATIC
    config.cpp
    clock.cpp
    exceptions.cpp
    diagnostics.cpp
    random_generator.cpp
    shutdown_token.cpp
    managed_thread.cpp
    thread_safe_queue.cpp
    data_model.cpp
    realtime_manager.cpp
    pi_mutex.cpp
    heartbeat_registry.cpp
    radar_producer.cpp
    fft.cpp
    signal_processor.cpp
    thread_pool.cpp
    console_sink.cpp
    file_sink.cpp
    logger.cpp
    watchdog.cpp
    metrics.cpp
    signal_handler.cpp
    application.cpp)

add_library(radar::core ALIAS radar_core)

target_include_directories(radar_core
    PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_compile_features(radar_core PUBLIC cxx_std_17)

# POSIX + GNU extensions are required only by some .cpp sources: the realtime ones
# (mlockall, pthread_setschedparam, pthread_setaffinity_np, PTHREAD_PRIO_INHERIT)
# and the signal handler (ppoll, signalfd), which the strict -std=c++17 dialect
# otherwise hides. The public headers need none of these (pi_mutex.hpp uses only the
# pthread_mutex_t type, signal_handler.hpp only an int fd), so this stays PRIVATE.
target_compile_definitions(radar_core PRIVATE _GNU_SOURCE)

# Warnings are private (the library is built with -Werror, but consumers are not
# forced to be). Sanitizers are public so instrumentation propagates to anything
# linking the library, including the tests. Threads is public because public
# headers (ShutdownToken) use std::mutex / std::condition_variable.
target_link_libraries(radar_core
    PUBLIC
        Threads::Threads
        radar::project_sanitizers
    PRIVATE
        radar::project_warnings)
