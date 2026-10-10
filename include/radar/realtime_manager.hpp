#pragma once

#include "radar/config.hpp"

#include <optional>

namespace radar {

// Outcome of a realtime configuration step. Realtime is best-effort: failures
// (typically missing privileges such as CAP_SYS_NICE or a low RLIMIT_RTPRIO)
// degrade gracefully instead of aborting, so the caller inspects this result and
// may log a warning while continuing at default scheduling.
struct RealtimeResult {
    bool applied{false};              // the realtime setting is active
    const char* failedCall{nullptr};  // name of the syscall that failed, or nullptr
    int errorNumber{0};               // errno-style code of the failure, or 0

    // True when no error occurred: either the setting was applied, or it was
    // intentionally skipped because realtime is disabled in configuration.
    [[nodiscard]] bool ok() const noexcept { return failedCall == nullptr; }
};

// Configures Linux realtime behaviour in the two stages required by the
// architecture (D2): a process-wide stage (memory locking + prefaulting) run once
// before threads start, and a per-thread stage (SCHED_FIFO scheduling and optional
// CPU affinity) run from inside each thread. Every system call is validated and
// failures are reported through RealtimeResult rather than thrown. The per-role
// priority is supplied by the caller from Config.
class RealtimeManager {
public:
    explicit RealtimeManager(const Config& config) noexcept : config_(config) {}

    RealtimeManager(const RealtimeManager&) = delete;
    RealtimeManager& operator=(const RealtimeManager&) = delete;
    RealtimeManager(RealtimeManager&&) = delete;
    RealtimeManager& operator=(RealtimeManager&&) = delete;
    ~RealtimeManager() = default;

    // Process-wide stage, run once before threads start. mlockall() locks all
    // current and future pages into RAM (POSIX-guaranteed on success, and the basis
    // of the returned RealtimeResult). It then prefaults the stack and heap, which
    // is a best-effort, glibc-specific optimization to reduce later page faults and
    // carries no hard guarantee. Skipped (no-op success) when memory locking is
    // disabled in Config.
    [[nodiscard]] RealtimeResult lockProcessMemory() const noexcept;

    // Per-thread stage: apply SCHED_FIFO at `priority` to the calling thread and,
    // when provided, pin it to `cpuAffinity`. Skipped (no-op success) when realtime
    // is disabled in Config. Must be called from the thread being configured.
    [[nodiscard]] RealtimeResult
    configureCurrentThread(int priority,
                           std::optional<unsigned> cpuAffinity = std::nullopt) const noexcept;

private:
    const Config& config_;
};

}  // namespace radar
