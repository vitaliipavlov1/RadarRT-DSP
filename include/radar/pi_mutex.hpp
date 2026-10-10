#pragma once

#include <pthread.h>

namespace radar {

// RAII wrapper around a POSIX mutex configured with the priority-inheritance
// protocol (PTHREAD_PRIO_INHERIT), to prevent priority inversion when threads of
// different realtime priorities contend for it (ARCHITECTURE.md).
//
// It satisfies the C++ BasicLockable / Lockable requirements (lock / try_lock /
// unlock), so it works with std::lock_guard, std::unique_lock and
// std::condition_variable_any. ThreadSafeQueue uses it as its internal lock (the
// container waits on condition_variable_any precisely because the lock is custom),
// so the shared queues are protected against priority inversion without any change
// to that container's public interface.
//
// Owns a pthread_mutex_t with a fixed address: non-copyable and non-movable.
class PiMutex {
public:
    PiMutex();
    ~PiMutex();

    PiMutex(const PiMutex&) = delete;
    PiMutex& operator=(const PiMutex&) = delete;
    PiMutex(PiMutex&&) = delete;
    PiMutex& operator=(PiMutex&&) = delete;

    void lock();
    [[nodiscard]] bool try_lock();
    void unlock() noexcept;

private:
    pthread_mutex_t mutex_;
};

}  // namespace radar
