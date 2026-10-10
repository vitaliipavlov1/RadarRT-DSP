#pragma once

#include "radar/fft.hpp"
#include "radar/noise_filter.hpp"
#include "radar/processed_signal.hpp"
#include "radar/radar_signal.hpp"
#include "radar/target_detection.hpp"

namespace radar {

// Stateless DSP pipeline: RadarSignal -> NoiseFilter -> FFT -> TargetDetection ->
// ProcessedSignal. The stages are plain classes composed by value (no inheritance,
// no interface); a new stage is added by extending this composition. Because it
// holds no mutable state, a single instance is safe to call concurrently from all
// DSP pool workers.
class SignalProcessor {
public:
    [[nodiscard]] ProcessedSignal process(const RadarSignal& signal) const noexcept {
        const RadarSignal::SampleBuffer filtered = noiseFilter_.apply(signal.samples());
        const FFT::Spectrum spectrum = fft_.apply(filtered);
        const TargetDetection::Detection detection = targetDetection_.apply(spectrum);
        return ProcessedSignal(signal.timestamp(), detection.targetDetected,
                               detection.peakMagnitude);
    }

private:
    NoiseFilter noiseFilter_;
    FFT fft_;
    TargetDetection targetDetection_;
};

}  // namespace radar
