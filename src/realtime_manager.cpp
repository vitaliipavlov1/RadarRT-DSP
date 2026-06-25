#include "radar/realtime_manager.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>

namespace radar {
namespace {

RealtimeResult applied() noexcept {
    return RealtimeResult{true, nullptr, 0};
}
RealtimeResult skipped() noexcept {
    return RealtimeResult{false, nullptr, 0};
}

RealtimeResult failure(const char* call, int errorNumber) noexcept {
    return RealtimeResult{false, call, errorNumber};
}

std::size_t pageSize() noexcept {
    const long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<std::size_t>(value) : static_cast<std::size_t>(4096);
}

// Best-effort prefault of the calling thread's stack: touch a block page by page
// so the pages are faulted in now (and, as this runs after mlockall(MCL_FUTURE),
// locked as they are mapped). An optimization to avoid first-touch faults during
// realtime execution; not guaranteed by POSIX to eliminate them.
void prefaultStack(std::size_t page) noexcept {
    constexpr std::size_t kBytes = 128 * 1024;
    // volatile so the touches are not optimized away; [[maybe_unused]] because the
    // pages are only written (to fault them in), never read back.
    [[maybe_unused]] volatile unsigned char buffer[kBytes];
    for (std::size_t offset = 0; offset < kBytes; offset += page) {
        buffer[offset] = 0;
    }
    buffer[kBytes - 1] = 0;
}

// Best-effort, glibc-specific prefault of the heap. mallopt() is a glibc extension
// (not POSIX); disabling trimming and mmap is a heuristic to keep freed memory in
// the arena so the touched (and MCL_FUTURE-locked) pages are reused by later
// allocations. None of this is guaranteed by POSIX or the C++ standard; it reduces,
// but does not eliminate, later allocation faults.
void prefaultHeap(std::size_t page) noexcept {
    constexpr std::size_t kBytes = 8 * 1024 * 1024;
    (void)mallopt(M_TRIM_THRESHOLD, -1);
    (void)mallopt(M_MMAP_MAX, 0);
    void* block = std::malloc(kBytes);
    if (block == nullptr) {
        return;
    }
    auto* bytes = static_cast<unsigned char*>(block);
    for (std::size_t offset = 0; offset < kBytes; offset += page) {
        bytes[offset] = 0;
    }
    std::free(block);
}

}  // namespace

RealtimeResult RealtimeManager::lockProcessMemory() const noexcept {
    if (!config_.realtime().lockMemory) {
        return skipped();
    }
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        return failure("mlockall", errno);
    }
    const std::size_t page = pageSize();
    prefaultStack(page);
    prefaultHeap(page);
    return applied();
}

RealtimeResult
RealtimeManager::configureCurrentThread(int priority,
                                        std::optional<unsigned> cpuAffinity) const noexcept {
    if (!config_.realtime().enabled) {
        return skipped();
    }

    sched_param parameters{};
    parameters.sched_priority = priority;
    // pthread_* return the error number directly rather than setting errno.
    const int scheduleResult = pthread_setschedparam(pthread_self(), SCHED_FIFO, &parameters);
    if (scheduleResult != 0) {
        return failure("pthread_setschedparam", scheduleResult);
    }

    if (cpuAffinity.has_value()) {
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        // Pass the unsigned CPU index directly: CPU_SET forwards it to a size_t
        // parameter, so an int would trigger a sign-conversion warning.
        CPU_SET(*cpuAffinity, &cpuSet);
        const int affinityResult = pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet);
        if (affinityResult != 0) {
            return failure("pthread_setaffinity_np", affinityResult);
        }
    }

    return applied();
}

}  // namespace radar
