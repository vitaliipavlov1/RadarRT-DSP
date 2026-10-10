#include "radar/managed_thread.hpp"

#include <thread>
#include <utility>

namespace radar {

ManagedThread& ManagedThread::operator=(ManagedThread&& other) noexcept {
    if (this != &other) {
        // Join the thread we currently own before taking ownership of another;
        // std::thread's own move assignment would call std::terminate here.
        join();
        thread_ = std::move(other.thread_);
    }
    return *this;
}

ManagedThread::~ManagedThread() {
    join();
}

// The single place that decides whether a join is needed, so the underlying
// join happens at most once and the logic is not duplicated.
void ManagedThread::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace radar
