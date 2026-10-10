#include "radar/random_generator.hpp"

#include <random>

namespace radar {

RandomGenerator::RandomGenerator(SeedType seed) : engine_(seed) {}

RandomGenerator::RandomGenerator() : engine_(std::random_device{}()) {}

int RandomGenerator::uniformInt(int low, int high) {
    std::uniform_int_distribution<int> distribution(low, high);
    return distribution(engine_);
}

double RandomGenerator::uniformReal(double low, double high) {
    std::uniform_real_distribution<double> distribution(low, high);
    return distribution(engine_);
}

}  // namespace radar
