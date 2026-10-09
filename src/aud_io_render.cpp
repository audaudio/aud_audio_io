// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The render functions of the package: the sine that proves a stream plays,
// the monitor that copies the input into the output, and the latency probe
// that measures the round trip from the output to the input on a device.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

#include "aud_io_internal.hpp"

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kSineHz = 440.0;
constexpr float kSineGain = 0.1f;  // -20 dBFS
constexpr float kDefaultThreshold = 0.1f;
// A click is a 5 ms burst of 2 kHz: small speakers and microphones carry
// it, where they would swallow a single pulse. It starts at its peak, so
// its onset is its first frame.
constexpr float kClickLevel = 0.8f;
constexpr double kClickHz = 2000.0;
constexpr double kClickSeconds = 0.005;

bool validRequest(const AudRenderRequest* request) {
  return request != nullptr &&
         request->struct_size >= sizeof(AudRenderRequest) &&
         request->time != nullptr &&
         (request->num_output_buses == 0 || request->outputs != nullptr) &&
         (request->num_input_buses == 0 || request->inputs != nullptr);
}

void raise(std::atomic<int64_t>& value, int64_t candidate) {
  if (candidate > value.load(std::memory_order_relaxed)) {
    value.store(candidate, std::memory_order_relaxed);
  }
}

void lower(std::atomic<int64_t>& value, int64_t candidate) {
  if (candidate < value.load(std::memory_order_relaxed)) {
    value.store(candidate, std::memory_order_relaxed);
  }
}

void raise(std::atomic<float>& value, float candidate) {
  if (candidate > value.load(std::memory_order_relaxed)) {
    value.store(candidate, std::memory_order_relaxed);
  }
}

}  // namespace

struct AudIoProbe {
  uint32_t intervalFrames = 0;
  float threshold = kDefaultThreshold;
  // The audio thread's state.
  bool started = false;
  int64_t nextClick = 0;      // the sample position of the next click
  int64_t clickPosition = 0;  // the position of the last click
  bool clicked = false;
  bool waiting = false;
  // What the control thread reads.
  std::atomic<int64_t> clicks{0};
  std::atomic<int64_t> detections{0};
  std::atomic<int64_t> last{0};
  std::atomic<int64_t> min{INT64_MAX};
  std::atomic<int64_t> max{0};
  std::atomic<int64_t> sum{0};
  std::atomic<int64_t> reported{0};
  std::atomic<float> inputPeak{0.0f};
};

AUD_EXPORT int32_t aud_io_sine_render(void* user,
                                      const AudRenderRequest* request) {
  if (!validRequest(request)) return AUD_ERROR_INVALID_ARGUMENT;
  const AudStreamTime& time = *request->time;
  const double rate = time.sample_rate > 0 ? time.sample_rate : 48000.0;
  // 440 Hz fits a whole number of cycles into every second, so the phase
  // follows from the position within the second.
  const int64_t second = std::max<int64_t>(1, std::llround(rate));
  int64_t position = time.sample_position % second;
  if (position < 0) position += second;
  const double increment = kTwoPi * kSineHz / rate;
  for (uint32_t f = 0; f < request->frames; ++f) {
    const float sample =
        static_cast<float>(std::sin(increment * static_cast<double>(position + f))) *
        kSineGain;
    for (uint32_t b = 0; b < request->num_output_buses; ++b) {
      const AudAudioBus& bus = request->outputs[b];
      for (uint32_t c = 0; c < bus.num_channels; ++c) bus.channels[c][f] = sample;
    }
  }
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_thru_render(void* user,
                                      const AudRenderRequest* request) {
  if (!validRequest(request)) return AUD_ERROR_INVALID_ARGUMENT;
  if (request->num_output_buses == 0) return AUD_OK;
  const AudAudioBus& output = request->outputs[0];
  const AudAudioBus* input =
      request->num_input_buses > 0 && request->inputs[0].num_channels > 0
          ? &request->inputs[0]
          : nullptr;
  const size_t bytes = sizeof(float) * request->frames;
  for (uint32_t c = 0; c < output.num_channels; ++c) {
    if (input == nullptr) {
      std::memset(output.channels[c], 0, bytes);
      continue;
    }
    // A mono input feeds every output channel.
    const uint32_t from = std::min(c, input->num_channels - 1);
    std::memcpy(output.channels[c], input->channels[from], bytes);
  }
  return AUD_OK;
}

AUD_EXPORT AudIoProbe* aud_io_probe_create(uint32_t interval_frames,
                                           float threshold) {
  if (interval_frames == 0 || threshold < 0) return nullptr;
  auto* probe = new (std::nothrow) AudIoProbe();
  if (probe == nullptr) return nullptr;
  probe->intervalFrames = interval_frames;
  if (threshold > 0) probe->threshold = threshold;
  return probe;
}

AUD_EXPORT void aud_io_probe_destroy(AudIoProbe* probe) { delete probe; }

AUD_EXPORT int32_t aud_io_probe_render(void* user,
                                       const AudRenderRequest* request) {
  auto* probe = static_cast<AudIoProbe*>(user);
  if (probe == nullptr || !validRequest(request)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  const AudStreamTime& time = *request->time;
  const int64_t start = time.sample_position;
  if (!probe->started) {
    probe->started = true;
    probe->nextClick = start + probe->intervalFrames / 2;
  }
  // A jump of the position after a recovery moves the clicks along.
  if (probe->nextClick < start - int64_t{probe->intervalFrames}) {
    probe->nextClick = start + probe->intervalFrames / 2;
    probe->waiting = false;
  }
  const float* input =
      request->num_input_buses > 0 && request->inputs[0].num_channels > 0
          ? request->inputs[0].channels[0]
          : nullptr;
  const double rate = time.sample_rate > 0 ? time.sample_rate : 48000.0;
  const int64_t clickFrames =
      std::max<int64_t>(1, std::llround(rate * kClickSeconds));
  float peak = 0.0f;
  for (uint32_t f = 0; f < request->frames; ++f) {
    const int64_t position = start + f;
    const float level = input != nullptr ? std::fabs(input[f]) : 0.0f;
    peak = std::max(peak, level);
    if (probe->waiting && level >= probe->threshold) {
      const int64_t roundTrip = position - probe->clickPosition;
      probe->waiting = false;
      probe->detections.fetch_add(1, std::memory_order_relaxed);
      probe->last.store(roundTrip, std::memory_order_relaxed);
      lower(probe->min, roundTrip);
      raise(probe->max, roundTrip);
      probe->sum.fetch_add(roundTrip, std::memory_order_relaxed);
      probe->reported.store(int64_t{time.output_latency_frames} +
                                time.input_latency_frames,
                            std::memory_order_relaxed);
    }
    if (probe->waiting &&
        position - probe->clickPosition >= probe->intervalFrames) {
      probe->waiting = false;  // lost in the loop
    }
    if (position == probe->nextClick) {
      probe->clickPosition = probe->nextClick;
      probe->nextClick += probe->intervalFrames;
      probe->clicked = true;
      probe->waiting = true;
      probe->clicks.fetch_add(1, std::memory_order_relaxed);
    }
    const int64_t age = position - probe->clickPosition;
    const bool clicking = probe->clicked && age >= 0 && age < clickFrames;
    const float sample =
        clicking ? kClickLevel * static_cast<float>(std::cos(
                                     kTwoPi * kClickHz * double(age) / rate))
                 : 0.0f;
    for (uint32_t b = 0; b < request->num_output_buses; ++b) {
      const AudAudioBus& bus = request->outputs[b];
      for (uint32_t c = 0; c < bus.num_channels; ++c) bus.channels[c][f] = sample;
    }
  }
  raise(probe->inputPeak, peak);
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_probe_read(AudIoProbe* probe,
                                     AudIoProbeResult* out) {
  if (probe == nullptr || out == nullptr ||
      out->struct_size < sizeof(AudIoProbeResult)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  const auto relaxed = std::memory_order_relaxed;
  out->clicks = probe->clicks.load(relaxed);
  out->detections = probe->detections.load(relaxed);
  out->last_round_trip_frames = probe->last.load(relaxed);
  out->min_round_trip_frames =
      out->detections == 0 ? 0 : probe->min.load(relaxed);
  out->max_round_trip_frames = probe->max.load(relaxed);
  out->sum_round_trip_frames = probe->sum.load(relaxed);
  out->reported_round_trip_frames = probe->reported.load(relaxed);
  out->input_peak = probe->inputPeak.load(relaxed);
  return AUD_OK;
}
