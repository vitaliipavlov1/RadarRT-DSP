#include "radar/diagnostics.hpp"

#include "radar/realtime_manager.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace radar {

void reportRealtimeDegradation(const RealtimeResult& result, const char* role) noexcept {
    if (result.ok()) {
        return;  // applied, or skipped because realtime is disabled: nothing to warn about
    }
    try {
        // Build the whole line first and emit it in a single write so concurrent
        // warnings from different worker threads stay legible. failedCall is non-null
        // whenever ok() is false.
        std::string line = "[radar] realtime (";
        line += role;
        line += "): ";
        line += result.failedCall;
        line += " failed (errno ";
        line += std::to_string(result.errorNumber);
        line += "); continuing at default scheduling\n";
        std::cerr << line;
    } catch (...) {
        return;  // best-effort startup diagnostic; never perturb the caller's loop
    }
}

void reportUnhandledException(const char* role) noexcept {
    // ex.what() returns a pointer owned by the in-flight exception, which outlives this
    // call (it is destroyed only when the caller's catch handler completes), so storing
    // the pointer is safe and allocates nothing on this failure path.
    const char* detail = "no active exception";
    if (std::current_exception()) {
        try {
            throw;  // rethrow the exception the caller's catch handler is processing
        } catch (const std::exception& ex) {
            detail = ex.what();
        } catch (...) {
            detail = "unknown (non-std::exception) type";
        }
    }
    try {
        std::string line = "[radar] thread '";
        line += role;
        line += "' terminated by exception: ";
        line += detail;
        line += "; requesting graceful shutdown\n";
        std::cerr << line;
    } catch (...) {
        return;  // best-effort: a failure to emit must not escape this noexcept boundary
    }
}

}  // namespace radar
