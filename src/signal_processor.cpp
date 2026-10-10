#include "radar/signal_processor.hpp"

#include "radar/fft.hpp"
#include "radar/noise_filter.hpp"
#include "radar/target_detection.hpp"

#include <type_traits>

namespace radar {

// The pipeline stages carry no state, so a single SignalProcessor instance can be
// shared across all DSP workers and invoked concurrently (see D1 in ARCHITECTURE.md).
static_assert(std::is_empty_v<NoiseFilter>);
static_assert(std::is_empty_v<FFT>);
static_assert(std::is_empty_v<TargetDetection>);
static_assert(std::is_trivially_copyable_v<SignalProcessor>);

namespace {

// Compile-time check that the full inline pipeline instantiates cleanly under the
// project warning set (process() is otherwise only type-checked when used).
// Never executed.
[[maybe_unused]] ProcessedSignal pipelineCompileCheck(const SignalProcessor& processor,
                                                      const RadarSignal& signal) {
    return processor.process(signal);
}

}  // namespace

}  // namespace radar
