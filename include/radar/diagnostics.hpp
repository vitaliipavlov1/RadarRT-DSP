#pragma once

namespace radar {

struct RealtimeResult;  // radar/realtime_manager.hpp

// Best-effort diagnostics written to stderr, for the few infrastructure events that
// cannot use the asynchronous Logger and must never perturb their caller:
//
//   * a per-thread realtime configuration step that degraded — the producer has no
//     access to the log queue, so reporting must not depend on it (realtime
//     configuration degrades gracefully with a warning);
//   * a thread's top-level exception boundary (D7), where the Logger may itself be the
//     failing thread, or the output queue may already be closing during the drain.
//
// stderr is the same channel the process-wide realtime warning already uses
// (Application::run); these helpers extend that single convention to the per-thread
// stages rather than introducing a second diagnostic path. Both functions are
// noexcept and swallow any internal failure, so they are safe to call from inside a
// thread's try block and from inside a catch handler. They run off the realtime hot
// path (thread entry / thread death), one line at most.

// Warn when a realtime configuration step failed. No-op when it was applied, or was
// skipped because realtime is disabled in Config (RealtimeResult::ok()).
void reportRealtimeDegradation(const RealtimeResult& result, const char* role) noexcept;

// Log the exception currently being handled (its what(), when it derives from
// std::exception). Precondition: call only from within a catch handler.
void reportUnhandledException(const char* role) noexcept;

}  // namespace radar
