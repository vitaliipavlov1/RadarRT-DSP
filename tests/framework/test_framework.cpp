#include "test_framework.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace radar::test {
namespace {

struct RegisteredTest {
    std::string suite;
    std::string name;
    TestFunction function;
};

// Meyers singleton: the registry must already exist when TestRegistrar instances
// run during static initialization, regardless of translation-unit order.
std::vector<RegisteredTest>& registry() {
    static std::vector<RegisteredTest> tests;
    return tests;
}

}  // namespace

void TestContext::recordFailure(const std::string& message) {
    failures_.fetch_add(1, std::memory_order_acq_rel);
    // One line per failure; std::cerr is unbuffered and internally synchronized,
    // so concurrent failures from worker threads stay legible.
    std::cerr << "    [FAIL] " << message << '\n';
}

TestRegistrar::TestRegistrar(const char* suite, const char* name, TestFunction function) {
    registry().push_back(RegisteredTest{suite, name, function});
}

namespace detail {

void reportFailure(TestContext& ctx, const std::string& message, const char* file, int line,
                   bool fatal) {
    std::ostringstream location;
    location << message << " (" << file << ":" << line << ")";
    ctx.recordFailure(location.str());
    if (fatal) {
        throw FatalAssertion{};
    }
}

}  // namespace detail

int runAllTests(const char* suiteFilter) {
    int runCount = 0;
    int failedCount = 0;

    for (const RegisteredTest& test : registry()) {
        if (suiteFilter != nullptr && test.suite != suiteFilter) {
            continue;
        }
        ++runCount;
        std::cout << "[ RUN  ] " << test.suite << '.' << test.name << '\n';

        TestContext ctx;
        try {
            test.function(ctx);
        } catch (const FatalAssertion&) {
            // The failure was already recorded; the throw only aborts the test.
        } catch (const std::exception& error) {
            ctx.recordFailure(std::string("unexpected exception: ") + error.what());
        } catch (...) {
            ctx.recordFailure("unexpected non-standard exception");
        }

        if (ctx.failed()) {
            ++failedCount;
            std::cout << "[ FAIL ] " << test.suite << '.' << test.name << " (" << ctx.failureCount()
                      << " failure(s))\n";
        } else {
            std::cout << "[  OK  ] " << test.suite << '.' << test.name << '\n';
        }
    }

    std::cout << '\n' << (runCount - failedCount) << '/' << runCount << " tests passed";
    if (suiteFilter != nullptr) {
        std::cout << " (suite '" << suiteFilter << "')";
    }
    std::cout << ".\n";
    return failedCount;
}

}  // namespace radar::test

int main(int argc, char** argv) {
    const char* suiteFilter = (argc > 1) ? argv[1] : nullptr;
    return radar::test::runAllTests(suiteFilter) == 0 ? 0 : 1;
}
