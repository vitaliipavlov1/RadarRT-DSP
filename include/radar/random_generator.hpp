#pragma once

#include <random>

namespace radar {

// Deterministic pseudo-random source used to synthesise radar signals.
//
// Each owner holds its own instance; this type is intentionally NOT thread-safe,
// so there is no shared mutable engine across threads. Seeding explicitly makes
// generated sequences reproducible in tests.
class RandomGenerator {
public:
    using SeedType = std::mt19937::result_type;

    explicit RandomGenerator(SeedType seed);
    RandomGenerator();  // seeded from std::random_device

    // Closed interval [low, high]; caller guarantees low <= high.
    [[nodiscard]] int uniformInt(int low, int high);
    [[nodiscard]] double uniformReal(double low, double high);

private:
    std::mt19937 engine_;
};

}  // namespace radar
