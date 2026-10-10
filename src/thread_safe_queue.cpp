#include "radar/thread_safe_queue.hpp"

namespace radar {
namespace {

// Move-only element type, used only to prove the queue never silently copies the
// values it stores.
struct MoveOnly {
    MoveOnly() = default;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
};

}  // namespace

// Compile-time validation that the whole template instantiates cleanly under the
// project warning set, for both a trivially-copyable and a move-only element
// type. Behavioural tests live in tests/test_thread_safe_queue.cpp.
template class ThreadSafeQueue<int>;
template class ThreadSafeQueue<MoveOnly>;

}  // namespace radar
