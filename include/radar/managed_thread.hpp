#pragma once

#include <thread>
#include <type_traits>
#include <utility>

namespace radar {

// RAII wrapper around std::thread that guarantees the thread is joined.
//
// It carries no stop logic and no stop callback: signalling a thread to stop
// before it is joined is the owner's (Application's) responsibility, per
// ARCHITECTURE.md. The destructor join is a safety net, not the primary
// shutdown mechanism. Detached threads are prohibited.
//
// Move-only: ownership of the underlying thread transfers; copying is forbidden.
class ManagedThread {
public:
    ManagedThread() noexcept = default;

    // Starts a thread running `function(args...)`. The enable_if guard stops this
    // perfect-forwarding constructor from hijacking the move constructor when the
    // first argument is itself a ManagedThread. If std::thread construction
    // throws, the exception propagates and no thread is leaked (the member was
    // never constructed).
    template <class Function, class... Args,
              class = std::enable_if_t<!std::is_same_v<std::decay_t<Function>, ManagedThread>>>
    explicit ManagedThread(Function&& function, Args&&... args)
        : thread_(std::forward<Function>(function), std::forward<Args>(args)...) {}

    ManagedThread(const ManagedThread&) = delete;
    ManagedThread& operator=(const ManagedThread&) = delete;

    ManagedThread(ManagedThread&&) noexcept = default;
    ManagedThread& operator=(ManagedThread&& other) noexcept;

    ~ManagedThread();

    [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }

    // Joins the owned thread. Idempotent: a no-op if there is no joinable thread,
    // so the underlying join happens at most once.
    void join();

private:
    std::thread thread_;
};

}  // namespace radar
