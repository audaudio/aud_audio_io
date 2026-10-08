// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The callback timing counters every backend keeps: block sizes, the period
// between callback starts, late callbacks and the time spent rendering.

#ifndef AUD_IO_STATS_HPP
#define AUD_IO_STATS_HPP

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "aud_audio_io.h"
#include "aud_clock.h"

struct AudIoStatsCollector {
  double sampleRate = 48000.0;
  std::atomic<uint64_t> callbacks{0};
  std::atomic<uint64_t> frames{0};
  std::atomic<uint32_t> framesMin{UINT32_MAX};
  std::atomic<uint32_t> framesMax{0};
  std::atomic<int64_t> periodMin{INT64_MAX};
  std::atomic<int64_t> periodMax{0};
  std::atomic<int64_t> periodSum{0};
  std::atomic<uint64_t> periodCount{0};
  std::atomic<uint64_t> late{0};
  std::atomic<uint64_t> disconnects{0};
  std::atomic<int64_t> callbackMax{0};
  std::atomic<int64_t> callbackSum{0};
  int64_t lastStart = 0;    // realtime thread only
  uint32_t lastFrames = 0;  // realtime thread only

  // [realtime] Records the start of a callback of `blockFrames` frames and
  // returns its start time.
  int64_t begin(uint32_t blockFrames) {
    const int64_t now = aud_clock_now_ns();
    if (lastStart != 0) {
      const int64_t period = now - lastStart;
      periodMin.store(std::min(periodMin.load(std::memory_order_relaxed), period),
                      std::memory_order_relaxed);
      periodMax.store(std::max(periodMax.load(std::memory_order_relaxed), period),
                      std::memory_order_relaxed);
      periodSum.fetch_add(period, std::memory_order_relaxed);
      periodCount.fetch_add(1, std::memory_order_relaxed);
      // The previous block's duration is the period that was due.
      const double expected = static_cast<double>(lastFrames) * 1e9 / sampleRate;
      if (static_cast<double>(period) > 1.5 * expected) {
        late.fetch_add(1, std::memory_order_relaxed);
      }
    }
    lastStart = now;
    lastFrames = blockFrames;
    callbacks.fetch_add(1, std::memory_order_relaxed);
    frames.fetch_add(blockFrames, std::memory_order_relaxed);
    framesMin.store(std::min(framesMin.load(std::memory_order_relaxed), blockFrames),
                    std::memory_order_relaxed);
    framesMax.store(std::max(framesMax.load(std::memory_order_relaxed), blockFrames),
                    std::memory_order_relaxed);
    return now;
  }

  // [realtime] Records the end of the callback that started at `start`.
  void end(int64_t start) {
    const int64_t elapsed = aud_clock_now_ns() - start;
    callbackMax.store(std::max(callbackMax.load(std::memory_order_relaxed), elapsed),
                      std::memory_order_relaxed);
    callbackSum.fetch_add(elapsed, std::memory_order_relaxed);
  }

  // [control] Zeroes the counters; the next period starts fresh.
  void reset() {
    callbacks = 0;
    frames = 0;
    framesMin = UINT32_MAX;
    framesMax = 0;
    periodMin = INT64_MAX;
    periodMax = 0;
    periodSum = 0;
    periodCount = 0;
    late = 0;
    callbackMax = 0;
    callbackSum = 0;
    lastStart = 0;
    lastFrames = 0;
  }

  // [control] Copies the counters into `stats`; the backend fills xruns and
  // the output latency afterwards.
  void copyTo(AudIoStats* stats) const {
    const uint64_t count = callbacks.load(std::memory_order_relaxed);
    stats->callbacks = count;
    stats->frames = frames.load(std::memory_order_relaxed);
    stats->frames_min = count == 0 ? 0 : framesMin.load(std::memory_order_relaxed);
    stats->frames_max = framesMax.load(std::memory_order_relaxed);
    const uint64_t periods = periodCount.load(std::memory_order_relaxed);
    stats->period_min_ns =
        periods == 0 ? 0 : periodMin.load(std::memory_order_relaxed);
    stats->period_max_ns = periodMax.load(std::memory_order_relaxed);
    stats->period_sum_ns = periodSum.load(std::memory_order_relaxed);
    stats->period_count = periods;
    stats->late_callbacks = late.load(std::memory_order_relaxed);
    stats->disconnects = disconnects.load(std::memory_order_relaxed);
    stats->callback_time_max_ns = callbackMax.load(std::memory_order_relaxed);
    stats->callback_time_sum_ns = callbackSum.load(std::memory_order_relaxed);
    stats->xruns = 0;
    stats->output_latency_ms = 0.0;
  }
};

#endif  // AUD_IO_STATS_HPP
