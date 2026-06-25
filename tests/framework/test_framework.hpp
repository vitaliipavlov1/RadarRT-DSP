#pragma once

// Minimal in-repo unit-test harness.
//
// The repository requires unit tests but disallows external test
// libraries by default. Rather than pull in GoogleTest/Catch2 via FetchContent
// (which would add a network dependency to every clean build and CI run), the
// project uses this self-contained harness. It is deliberately small: a static
// registry of test functions, a thread-safe per-test failure accumulator, and a
// handful of assertion macros. The decision is recorded in README.md and
// tests/CMakeLists.txt.
//
// Usage:
//
//     RADAR_TEST(suite, name) {
//         RADAR_EXPECT(condition);          // non-fatal: records, keeps going
//         RADAR_ASSERT(condition);          // fatal: records, aborts this test
//         RADAR_EXPECT_EQ(actual, expected);
//     }
//
// Each test is a free function that self-registers during static initialization.
// A single main() (in test_framework.cpp) runs every registered test, or only
// those of one suite when a suite name is passed on the command line — which is
// how CTest invokes one logical group at a time.

#include <atomic>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace radar::test {

// Thrown by a fatal assertion to abort the current test immediately. The runner
// catches it; the failure has already been recorded, so it is a control-flow
// signal only and carries no payload.
struct FatalAssertion {};

// Per-test result accumulator. A test may spawn worker threads that assert, so
// failure counting is atomic; the human-readable diagnostic is written to
// std::cerr at the point of failure.
class TestContext {
public:
    void recordFailure(const std::string& message);

    [[nodiscard]] bool failed() const noexcept {
        return failures_.load(std::memory_order_acquire) != 0;
    }
    [[nodiscard]] int failureCount() const noexcept {
        return failures_.load(std::memory_order_acquire);
    }

private:
    std::atomic<int> failures_{0};
};

using TestFunction = void (*)(TestContext&);

// Self-registers a single test during static initialization. One instance is
// created per RADAR_TEST.
class TestRegistrar {
public:
    TestRegistrar(const char* suite, const char* name, TestFunction function);
};

// Runs every registered test whose suite matches `suiteFilter` (nullptr = all).
// Returns the number of FAILED tests, suitable as a process exit status.
[[nodiscard]] int runAllTests(const char* suiteFilter);

namespace detail {

// Streams a value when its type supports operator<<, otherwise yields a
// placeholder. This keeps assertion diagnostics informative for common types
// (ints, std::string, ...) without forcing every compared type to be streamable
// (enums, std::optional, time points, ...).
template <class T, class = void>
struct Printer {
    static void print(std::ostream& os, const T&) { os << "<?>"; }
};
template <class T>
struct Printer<T,
               std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>> {
    static void print(std::ostream& os, const T& value) { os << value; }
};

template <class T>
std::string toString(const T& value) {
    std::ostringstream os;
    Printer<T>::print(os, value);
    return os.str();
}

// Records a failure (and throws to abort the test when `fatal`). Defined in
// test_framework.cpp so the throw site is in one place.
void reportFailure(TestContext& ctx, const std::string& message, const char* file, int line,
                   bool fatal);

inline void check(TestContext& ctx, bool ok, const char* expression, const char* file, int line,
                  bool fatal) {
    if (!ok) {
        reportFailure(ctx, std::string("expected: ") + expression, file, line, fatal);
    }
}

template <class A, class B>
void checkEqual(TestContext& ctx, const A& actual, const B& expected, const char* actualExpr,
                const char* expectedExpr, const char* file, int line, bool fatal) {
    if (!(actual == expected)) {
        std::string message = std::string("expected ") + actualExpr + " == " + expectedExpr +
                              " (actual: " + toString(actual) +
                              ", expected: " + toString(expected) + ")";
        reportFailure(ctx, message, file, line, fatal);
    }
}

}  // namespace detail
}  // namespace radar::test

// Defines and self-registers a test. The trailing body is supplied by the caller.
// `ctx` is the per-test context the assertion macros below operate on; it is
// marked maybe_unused so a (rare) assertion-free test still compiles warning-clean.
#define RADAR_TEST(suite, name)                                                                    \
    static void radar_test_##suite##_##name(radar::test::TestContext& ctx);                        \
    namespace {                                                                                    \
    const ::radar::test::TestRegistrar                                                             \
        radar_registrar_##suite##_##name(#suite, #name, &radar_test_##suite##_##name);             \
    }                                                                                              \
    static void radar_test_##suite##_##name([[maybe_unused]] radar::test::TestContext& ctx)

#define RADAR_EXPECT(condition)                                                                    \
    ::radar::test::detail::check(ctx, static_cast<bool>(condition), #condition, __FILE__,          \
                                 __LINE__, false)

#define RADAR_ASSERT(condition)                                                                    \
    ::radar::test::detail::check(ctx, static_cast<bool>(condition), #condition, __FILE__,          \
                                 __LINE__, true)

#define RADAR_EXPECT_EQ(actual, expected)                                                          \
    ::radar::test::detail::checkEqual(ctx, (actual), (expected), #actual, #expected, __FILE__,     \
                                      __LINE__, false)

#define RADAR_ASSERT_EQ(actual, expected)                                                          \
    ::radar::test::detail::checkEqual(ctx, (actual), (expected), #actual, #expected, __FILE__,     \
                                      __LINE__, true)

#define RADAR_FAIL(message)                                                                        \
    ::radar::test::detail::reportFailure(ctx, (message), __FILE__, __LINE__, true)
