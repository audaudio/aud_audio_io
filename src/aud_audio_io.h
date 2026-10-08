// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The C API of the spike stream of aud_audio_io (ticket 5, S0-mobile): one
// output stream per device that pulls interleaved float blocks from an
// AudRenderCallback and measures its callback timing. miniaudio serves
// macOS, iOS, Windows and Linux (io-001), Oboe serves Android (io-002).
// Ticket S3 adds enumeration, hot-plug, duplex streams and the timestamps
// of time-001.

#ifndef AUD_AUDIO_IO_H
#define AUD_AUDIO_IO_H

#include <stdint.h>

#include "aud_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudIoStream AudIoStream;

// How a stream is opened.
typedef struct AudIoConfig {
  uint32_t struct_size;
  double sample_rate;            // 0 = the device's native rate
  uint32_t channels;             // output channels
  uint32_t frames_per_callback;  // 0 = the backend's default
  int32_t use_null_backend;      // 1 = a silent device that keeps time (tests)
} AudIoConfig;

// Counters the stream callback keeps; read with aud_io_get_stats.
typedef struct AudIoStats {
  uint32_t struct_size;
  uint32_t frames_min;  // smallest callback block
  uint32_t frames_max;  // largest callback block
  uint64_t callbacks;
  uint64_t frames;
  int64_t period_min_ns;  // time between callback starts
  int64_t period_max_ns;
  int64_t period_sum_ns;
  uint64_t period_count;
  uint64_t late_callbacks;  // period above 1.5 times the block duration
  uint64_t xruns;           // reported by the backend; Oboe only
  uint64_t disconnects;     // the backend closed the stream (route change)
  int64_t callback_time_max_ns;  // time spent inside the callback
  int64_t callback_time_sum_ns;
  double output_latency_ms;  // reported by the backend; Oboe only, else 0
} AudIoStats;

// [control] Opens an output stream that calls `render` with `user` from the
// audio thread. Returns NULL when the device cannot be opened.
AUD_EXPORT AudIoStream* aud_io_open(const AudIoConfig* config,
                                    AudRenderCallback render, void* user);

// [control] Starts the callbacks; AUD_ERROR_STATE when already started.
AUD_EXPORT int32_t aud_io_start(AudIoStream* stream);

// [control] Stops the callbacks and waits for the last one to return.
AUD_EXPORT int32_t aud_io_stop(AudIoStream* stream);

// [control] Closes the stream and the device.
AUD_EXPORT void aud_io_close(AudIoStream* stream);

// [control] The sample rate the device runs at.
AUD_EXPORT double aud_io_sample_rate(AudIoStream* stream);

// [control] The output channels.
AUD_EXPORT uint32_t aud_io_channels(AudIoStream* stream);

// [control] The negotiated block size; 0 when the backend does not say.
AUD_EXPORT uint32_t aud_io_frames_per_callback(AudIoStream* stream);

// [control] The backend, e.g. "miniaudio/Core Audio" or "oboe/aaudio".
AUD_EXPORT const char* aud_io_backend_name(AudIoStream* stream);

// [control] Copies the counters.
AUD_EXPORT void aud_io_get_stats(AudIoStream* stream, AudIoStats* stats);

// [control] Zeroes the counters.
AUD_EXPORT void aud_io_reset_stats(AudIoStream* stream);

// [realtime] A render callback that fills the output with a 440 Hz sine at
// -20 dBFS, the smoke signal of this package; `user` is ignored.
AUD_EXPORT void aud_io_sine_render(void* user, float* interleaved_output,
                                   uint32_t frames, uint32_t channels);

#ifdef __cplusplus
}
#endif

#endif  // AUD_AUDIO_IO_H
