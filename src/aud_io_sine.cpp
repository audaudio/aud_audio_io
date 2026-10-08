// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include <cmath>

#include "aud_audio_io.h"

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kFrequencyHz = 440.0;
constexpr double kAssumedSampleRate = 48000.0;
constexpr float kGain = 0.1f;  // -20 dBFS

double phase = 0.0;  // the realtime thread owns it

}  // namespace

AUD_EXPORT void aud_io_sine_render(void*, float* interleaved_output,
                                   uint32_t frames, uint32_t channels) {
  const double increment = kTwoPi * kFrequencyHz / kAssumedSampleRate;
  for (uint32_t frame = 0; frame < frames; ++frame) {
    const float sample = static_cast<float>(std::sin(phase)) * kGain;
    for (uint32_t channel = 0; channel < channels; ++channel) {
      interleaved_output[frame * channels + channel] = sample;
    }
    phase += increment;
    if (phase >= kTwoPi) phase -= kTwoPi;
  }
}
