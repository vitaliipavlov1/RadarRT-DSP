#include "radar/pi_mutex.hpp"

#include "radar/exceptions.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <pthread.h>

namespace radar {

PiMutex::PiMutex() {
    pthread_mutexattr_t attributes;
    int result = pthread_mutexattr_init(&attributes);
    if (result != 0) {
        throw SystemCallException("pthread_mutexattr_init", result);
    }

    result = pthread_mutexattr_setprotocol(&attributes, PTHREAD_PRIO_INHERIT);
    if (result != 0) {
        (void)pthread_mutexattr_destroy(&attributes);
        throw SystemCallException("pthread_mutexattr_setprotocol", result);
    }

    result = pthread_mutex_init(&mutex_, &attributes);
    const int destroyResult = pthread_mutexattr_destroy(&attributes);
    if (result != 0) {
        throw SystemCallException("pthread_mutex_init", result);
    }
    if (destroyResult != 0) {
        (void)pthread_mutex_destroy(&mutex_);
        throw SystemCallException("pthread_mutexattr_destroy", destroyResult);
    }
}

PiMutex::~PiMutex() {
    // Destroying a still-locked mutex is a programming error; nothing safe can be
    // done in a destructor, so the result is intentionally discarded.
    (void)pthread_mutex_destroy(&mutex_);
}

void PiMutex::lock() {
    const int result = pthread_mutex_lock(&mutex_);
    if (result != 0) {
        throw SystemCallException("pthread_mutex_lock", result);
    }
}

bool PiMutex::try_lock() {
    const int result = pthread_mutex_trylock(&mutex_);
    if (result == 0) {
        return true;
    }
    if (result == EBUSY) {
        return false;
    }
    throw SystemCallException("pthread_mutex_trylock", result);
}

void PiMutex::unlock() noexcept {
    // unlock() must be noexcept for use with std::lock_guard / std::unique_lock.
    // A non-zero result indicates misuse (unlocking a mutex not owned) and cannot
    // be meaningfully handled here, so it is intentionally discarded.
    (void)pthread_mutex_unlock(&mutex_);
}

namespace {

// Compile-time proof that PiMutex is a BasicLockable usable with the standard
// lock wrappers and condition_variable_any — the exact contract ThreadSafeQueue
// relies on for its internal lock. Never executed; instantiated only to be
// type-checked.
[[maybe_unused]] void integrationCompileCheck() {
    PiMutex mutex;
    std::unique_lock<PiMutex> lock(mutex);
    std::condition_variable_any condition;
    (void)condition.wait_for(lock, std::chrono::milliseconds(0), [] { return true; });
}

}  // namespace

}  // namespace radar
