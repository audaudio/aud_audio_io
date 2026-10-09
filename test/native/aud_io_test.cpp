// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The native tests of aud_audio_io on the null backend (ticket 21):
// splitting and de-interleaving, the stream time, the hold after a format
// change, recovery, interruptions, hot-plugs, counters, notifications and
// the render functions. scripts/test-native.js runs them under the address
// and undefined behaviour sanitizers, the RealtimeSanitizer and the thread
// sanitizer.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "aud_audio_io.h"
#include "aud_clock.h"
#include "aud_test.hpp"

// A test that breaks the realtime contract on purpose tells the
// RealtimeSanitizer so, unless AUD_TEST_RTSAN_PROBE is set:
// scripts/test-native.js then checks that the rtsan run catches it.
#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#include <sanitizer/rtsan_interface.h>
#define AUD_TEST_RTSAN_OFF() __rtsan_disable()
#define AUD_TEST_RTSAN_ON() __rtsan_enable()
#endif
#endif
#ifndef AUD_TEST_RTSAN_OFF
#define AUD_TEST_RTSAN_OFF()
#define AUD_TEST_RTSAN_ON()
#endif

namespace {

constexpr double kRate = 48000;
constexpr uint32_t kBuffer = 256;  // the null device's default buffer
constexpr int64_t kNsPerFrame = 20833;  // at 48 kHz, rounded down

int64_t now() { return aud_clock_now_ns(); }

void sleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int64_t framesToNs(int64_t frames, double rate = kRate) {
  return static_cast<int64_t>(std::llround(frames * 1e9 / rate));
}

// ............................................................................
// Render functions of the tests

// Records the blocks it renders; used with the manual clock only, where
// the test thread is the audio thread.
struct Recorder {
  static constexpr size_t kCapacity = 1024;
  size_t count = 0;
  AudStreamTime times[kCapacity];
  uint32_t inputBuses[kCapacity];
  uint32_t outputBuses[kCapacity];
  uint32_t inputChannels[kCapacity];
  uint32_t outputChannels[kCapacity];
  int32_t result = AUD_OK;
  bool copyInput = false;  // otherwise a pattern of channel and position
};

int32_t recordRender(void* user, const AudRenderRequest* request) {
  auto* recorder = static_cast<Recorder*>(user);
  if (recorder->count < Recorder::kCapacity) {
    const size_t i = recorder->count++;
    recorder->times[i] = *request->time;
    recorder->inputBuses[i] = request->num_input_buses;
    recorder->outputBuses[i] = request->num_output_buses;
    recorder->inputChannels[i] =
        request->num_input_buses > 0 ? request->inputs[0].num_channels : 0;
    recorder->outputChannels[i] =
        request->num_output_buses > 0 ? request->outputs[0].num_channels : 0;
  }
  if (request->num_output_buses > 0) {
    const AudAudioBus& output = request->outputs[0];
    for (uint32_t c = 0; c < output.num_channels; ++c) {
      for (uint32_t f = 0; f < request->frames; ++f) {
        if (recorder->copyInput && request->num_input_buses > 0 &&
            c < request->inputs[0].num_channels) {
          output.channels[c][f] = request->inputs[0].channels[c][f];
        } else {
          output.channels[c][f] = static_cast<float>(
              c * 100000 + request->time->sample_position + f);
        }
      }
    }
  }
  return recorder->result;
}

// Breaks the realtime contract: allocates on the audio thread.
const bool g_rtsanProbe = std::getenv("AUD_TEST_RTSAN_PROBE") != nullptr;
void* volatile g_sink = nullptr;

int32_t allocatingRender(void* user, const AudRenderRequest* request) {
  if (!g_rtsanProbe) AUD_TEST_RTSAN_OFF();
  char* bytes = new char[64];
  g_sink = bytes;
  delete[] static_cast<char*>(g_sink);
  if (!g_rtsanProbe) AUD_TEST_RTSAN_ON();
  return aud_io_sine_render(user, request);
}

// ............................................................................
// Sessions, streams and notifications

AudIoSession* makeSession(uint32_t flags = AUD_IO_SESSION_MANUAL_CLOCK,
                          uint32_t timeoutMs = 0, uint32_t capacity = 0) {
  AudIoSessionConfig config{};
  config.struct_size = sizeof(AudIoSessionConfig);
  config.backend = AUD_IO_BACKEND_NULL;
  config.directions = AUD_IO_DUPLEX;
  config.flags = flags;
  config.notification_capacity = capacity;
  config.recovery_timeout_ms = timeoutMs;
  return aud_io_session_create(&config);
}

AudIoStreamConfig makeConfig(AudRenderFunction render, void* user,
                             uint32_t direction = AUD_IO_OUTPUT) {
  AudIoStreamConfig config{};
  config.struct_size = sizeof(AudIoStreamConfig);
  config.direction = direction;
  config.render = render;
  config.render_user = user;
  return config;
}

AudIoStream* open(AudIoSession* session, const AudIoStreamConfig& config) {
  int32_t result = AUD_ERROR_FAILED;
  AudIoStream* stream = aud_io_stream_open(session, &config, &result);
  AUD_CHECK(stream != nullptr);
  AUD_CHECK(result == AUD_OK);
  return stream;
}

AudIoStreamFormat formatOf(AudIoStream* stream) {
  AudIoStreamFormat format{};
  format.struct_size = sizeof(AudIoStreamFormat);
  AUD_CHECK(aud_io_stream_format(stream, &format) == AUD_OK);
  return format;
}

AudIoCounters countersOf(AudIoStream* stream) {
  AudIoCounters counters{};
  counters.struct_size = sizeof(AudIoCounters);
  AUD_CHECK(aud_io_stream_counters(stream, &counters) == AUD_OK);
  return counters;
}

// Collects the notifications of a session in order.
struct Inbox {
  explicit Inbox(AudIoSession* owner) : session(owner) {}

  void pump() {
    AudIoNotification batch[16];
    while (true) {
      const int32_t count =
          aud_io_session_take_notifications(session, batch, 16);
      for (int32_t i = 0; i < count; ++i) seen.push_back(batch[i]);
      if (count < 16) return;
    }
  }

  // Waits for the next notification of `type` after the last one found.
  bool waitFor(uint32_t type, AudIoNotification* out = nullptr,
               int ms = 2000) {
    const int64_t deadline = now() + int64_t{ms} * 1'000'000;
    while (true) {
      pump();
      for (size_t i = cursor; i < seen.size(); ++i) {
        if (seen[i].type == type) {
          cursor = i + 1;
          if (out != nullptr) *out = seen[i];
          return true;
        }
      }
      if (now() > deadline) return false;
      sleepMs(1);
    }
  }

  // Waits until `n` notifications of `type` arrived in all.
  bool waitCount(uint32_t type, size_t n, int ms = 2000) {
    const int64_t deadline = now() + int64_t{ms} * 1'000'000;
    while (count(type) < n) {
      if (now() > deadline) return false;
      sleepMs(1);
    }
    return true;
  }

  size_t count(uint32_t type) {
    pump();
    size_t n = 0;
    for (const AudIoNotification& notification : seen) {
      if (notification.type == type) n += 1;
    }
    return n;
  }

  AudIoSession* session;
  std::vector<AudIoNotification> seen;
  size_t cursor = 0;
};

bool waitState(AudIoStream* stream, int32_t state, int ms = 2000) {
  const int64_t deadline = now() + int64_t{ms} * 1'000'000;
  while (aud_io_stream_state(stream) != state) {
    if (now() > deadline) return false;
    sleepMs(1);
  }
  return true;
}

// Runs one manual callback of `frames` frames at `hostNs`; returns the
// interleaved output.
std::vector<float> process(AudIoStream* stream, uint32_t frames,
                           int64_t hostNs, uint32_t channels = 2,
                           const float* input = nullptr) {
  std::vector<float> output(size_t{frames} * channels, -1.0f);
  AUD_CHECK(aud_io_null_process(stream, input, output.data(), frames,
                                hostNs) == AUD_OK);
  return output;
}

}  // namespace

// ############################################################################
// Session

AUD_TEST(session_lists_the_null_devices) {
  AudIoSession* session = makeSession();
  AUD_CHECK(std::strcmp(aud_io_session_backend_name(session), "null") == 0);
  AUD_CHECK(aud_io_session_devices(session, nullptr, 0) == 2);
  AudIoDevice devices[4] = {};
  AUD_CHECK(aud_io_session_devices(session, devices, 1) == 2);
  AUD_CHECK(std::strcmp(devices[0].id, "null:out") == 0);
  AUD_CHECK(devices[1].struct_size == 0);  // beyond the capacity
  AUD_CHECK(aud_io_session_devices(session, devices, 4) == 2);
  AUD_CHECK(devices[0].directions == AUD_IO_OUTPUT);
  AUD_CHECK(devices[0].flags == AUD_IO_DEVICE_DEFAULT_OUTPUT);
  AUD_CHECK(devices[0].route == AUD_IO_ROUTE_VIRTUAL);
  AUD_CHECK(devices[0].max_output_channels == 2);
  AUD_CHECK(std::strcmp(devices[1].id, "null:in") == 0);
  AUD_CHECK(devices[1].directions == AUD_IO_INPUT);
  AUD_CHECK(devices[1].flags == AUD_IO_DEVICE_DEFAULT_INPUT);
  AUD_CHECK(devices[1].max_input_channels == 2);
  AUD_CHECK(aud_io_session_devices(session, nullptr, 1) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

AUD_TEST(session_refuses_invalid_configurations) {
  AUD_CHECK(aud_io_session_create(nullptr) == nullptr);
  AudIoSessionConfig config{};
  config.struct_size = sizeof(AudIoSessionConfig) - 4;
  AUD_CHECK(aud_io_session_create(&config) == nullptr);
  config.struct_size = sizeof(AudIoSessionConfig);
  config.backend = 7;
  AUD_CHECK(aud_io_session_create(&config) == nullptr);
  config.backend = AUD_IO_BACKEND_NULL;
  config.directions = 8;
  AUD_CHECK(aud_io_session_create(&config) == nullptr);
  // The platform backend of the desktop is the null backend until S3b.
  config.backend = AUD_IO_BACKEND_PLATFORM;
  config.directions = 0;
  AudIoSession* session = aud_io_session_create(&config);
  AUD_CHECK(session != nullptr);
  aud_io_session_destroy(session);
}

AUD_TEST(null_arguments_are_refused) {
  AUD_CHECK(std::strcmp(aud_io_session_backend_name(nullptr), "") == 0);
  AUD_CHECK(aud_io_session_devices(nullptr, nullptr, 0) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_session_permission(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_session_request_permission(nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_session_set_listener(nullptr, nullptr, nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_session_take_notifications(nullptr, nullptr, 0) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_session_interrupt(nullptr, 0) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_session_resume(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_start(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_stop(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_state(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_id(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_format(nullptr, nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_acknowledge(nullptr, 1) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_counters(nullptr, nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_reset_counters(nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_null_inject(nullptr, AUD_IO_FAULT_XRUN, 1) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_null_set_block_sizes(nullptr, nullptr, 0) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_null_process(nullptr, nullptr, nullptr, 1, 0) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_stream_close(nullptr);
  aud_io_session_destroy(nullptr);
}

AUD_TEST(open_refuses_invalid_configurations) {
  AudIoSession* session = makeSession();
  Recorder recorder;
  int32_t result = AUD_OK;
  AUD_CHECK(aud_io_stream_open(session, nullptr, &result) == nullptr);
  AUD_CHECK(result == AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_open(nullptr, nullptr, nullptr) == nullptr);
  const AudIoStreamConfig valid = makeConfig(recordRender, &recorder);
  const auto refused = [&](AudIoStreamConfig config, int32_t expected) {
    int32_t outcome = AUD_OK;
    AUD_CHECK(aud_io_stream_open(session, &config, &outcome) == nullptr);
    AUD_CHECK(outcome == expected);
  };
  AudIoStreamConfig config = valid;
  config.struct_size -= 4;
  refused(config, AUD_ERROR_INVALID_ARGUMENT);
  for (uint32_t direction : {0u, 4u}) {
    config = valid;
    config.direction = direction;
    refused(config, AUD_ERROR_INVALID_ARGUMENT);
  }
  config = valid;
  config.render = nullptr;
  refused(config, AUD_ERROR_INVALID_ARGUMENT);
  config = valid;
  config.output_channels = AUD_IO_MAX_CHANNELS + 1;
  refused(config, AUD_ERROR_INVALID_ARGUMENT);
  config = valid;
  config.max_frames = 16385;
  refused(config, AUD_ERROR_INVALID_ARGUMENT);
  config = valid;
  config.sample_rate = 1000;
  refused(config, AUD_ERROR_INVALID_ARGUMENT);
  config = valid;
  config.performance_mode = 3;
  refused(config, AUD_ERROR_INVALID_ARGUMENT);
  config = valid;
  config.output_device_id = "nowhere";
  refused(config, AUD_IO_ERROR_NO_DEVICE);
  config = makeConfig(recordRender, &recorder, AUD_IO_INPUT);
  config.input_device_id = "nowhere";
  refused(config, AUD_IO_ERROR_NO_DEVICE);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_FAIL_OPEN, 1) == AUD_OK);
  refused(valid, AUD_IO_ERROR_DEVICE);
  aud_io_session_destroy(session);
}

AUD_TEST(input_needs_the_microphone_permission) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AUD_CHECK(aud_io_session_permission(session) == AUD_IO_PERMISSION_GRANTED);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_PERMISSION,
                               AUD_IO_PERMISSION_DENIED) == AUD_OK);
  AudIoNotification notification{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_PERMISSION, &notification));
  AUD_CHECK(notification.code == AUD_IO_PERMISSION_DENIED);
  AudIoStreamConfig config = makeConfig(recordRender, &recorder, AUD_IO_DUPLEX);
  int32_t result = AUD_OK;
  AUD_CHECK(aud_io_stream_open(session, &config, &result) == nullptr);
  AUD_CHECK(result == AUD_IO_ERROR_PERMISSION);
  // Output needs none.
  config.direction = AUD_IO_OUTPUT;
  AudIoStream* output = open(session, config);
  aud_io_stream_close(output);
  // An undetermined permission is asked for; the null user grants it.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_PERMISSION,
                               AUD_IO_PERMISSION_UNDETERMINED) == AUD_OK);
  AUD_CHECK(aud_io_session_request_permission(session) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_PERMISSION, &notification));
  AUD_CHECK(notification.code == AUD_IO_PERMISSION_UNDETERMINED);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_PERMISSION, &notification));
  AUD_CHECK(notification.code == AUD_IO_PERMISSION_GRANTED);
  config.direction = AUD_IO_DUPLEX;
  AudIoStream* duplex = open(session, config);
  aud_io_stream_close(duplex);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_PERMISSION, 3) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

// ############################################################################
// The callback

AUD_TEST(output_is_split_into_blocks_and_interleaved) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AudIoStreamConfig config = makeConfig(recordRender, &recorder);
  config.max_frames = 64;
  AudIoStream* stream = open(session, config);
  const AudIoStreamFormat format = formatOf(stream);
  AUD_CHECK(format.direction == AUD_IO_OUTPUT);
  AUD_CHECK(format.sample_rate == kRate);
  AUD_CHECK(format.output_channels == 2);
  AUD_CHECK(format.input_channels == 0);
  AUD_CHECK(format.max_frames == 64);
  AUD_CHECK(format.buffer_frames == kBuffer);
  AUD_CHECK(format.burst_frames == kBuffer);
  AUD_CHECK(format.generation == 1);
  AUD_CHECK(format.time_source == AUD_TIME_SOURCE_HARDWARE);
  AUD_CHECK(format.exclusive == 0);
  AUD_CHECK(std::strcmp(format.output_device_id, "null:out") == 0);
  AUD_CHECK(std::strcmp(format.input_device_id, "") == 0);
  AUD_CHECK(std::strcmp(format.backend, "null") == 0);
  AUD_CHECK(aud_io_stream_id(stream) == 1);
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_STOPPED);
  // A stopped stream does not call back.
  float buffer[4] = {};
  AUD_CHECK(aud_io_null_process(stream, nullptr, buffer, 2, now()) ==
            AUD_ERROR_STATE);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_RUNNING);
  const int64_t t0 = now();
  const std::vector<float> output = process(stream, 200, t0);
  AUD_CHECK(recorder.count == 4);
  const uint32_t blocks[] = {64, 64, 64, 8};
  for (size_t i = 0; i < 4; ++i) {
    const AudStreamTime& time = recorder.times[i];
    AUD_CHECK(time.struct_size == sizeof(AudStreamTime));
    AUD_CHECK(time.frames == blocks[i]);
    AUD_CHECK(time.sample_position == int64_t(i * 64));
    AUD_CHECK(time.sample_rate == kRate);
    AUD_CHECK(time.host_time_source == AUD_TIME_SOURCE_HARDWARE);
    AUD_CHECK(time.output_latency_frames == kBuffer);
    AUD_CHECK(time.input_latency_frames == 0);
    AUD_CHECK_NEAR(double(time.host_time_ns),
                   double(t0 + framesToNs(kBuffer) + framesToNs(i * 64)), 2);
    AUD_CHECK(recorder.outputBuses[i] == 1);
    AUD_CHECK(recorder.inputBuses[i] == 0);
    AUD_CHECK(recorder.outputChannels[i] == 2);
  }
  bool interleaved = true;
  for (uint32_t f = 0; f < 200; ++f) {
    for (uint32_t c = 0; c < 2; ++c) {
      if (output[f * 2 + c] != float(c * 100000 + f)) interleaved = false;
    }
  }
  AUD_CHECK(interleaved);
  const AudIoCounters counters = countersOf(stream);
  AUD_CHECK(counters.state == AUD_IO_STATE_RUNNING);
  AUD_CHECK(counters.callbacks == 1);
  AUD_CHECK(counters.frames == 200);
  AUD_CHECK(counters.renders == 4);
  AUD_CHECK(counters.callback_frames_min == 200);
  AUD_CHECK(counters.callback_frames_max == 200);
  AUD_CHECK(counters.last_time.sample_position == 192);
  AUD_CHECK(counters.last_time.frames == 8);
  AUD_CHECK(counters.callback_time_max_ns > 0);
  AudIoNotification started{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STARTED, &started));
  AUD_CHECK(started.stream == 1);
  AUD_CHECK(started.value == 0);
  AUD_CHECK(started.sample_rate == kRate);
  aud_io_session_destroy(session);
}

AUD_TEST(duplex_input_is_deinterleaved) {
  AudIoSession* session = makeSession();
  Recorder recorder;
  recorder.copyInput = true;
  AudIoStreamConfig config =
      makeConfig(recordRender, &recorder, AUD_IO_DUPLEX);
  config.max_frames = 128;
  AudIoStream* stream = open(session, config);
  const AudIoStreamFormat format = formatOf(stream);
  AUD_CHECK(format.input_channels == 2);
  AUD_CHECK(std::strcmp(format.input_device_id, "null:in") == 0);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  std::vector<float> input(300 * 2);
  for (uint32_t f = 0; f < 300; ++f) {
    input[f * 2] = float(f) + 0.25f;
    input[f * 2 + 1] = -float(f);
  }
  const std::vector<float> output = process(stream, 300, now(), 2, input.data());
  AUD_CHECK(output == input);
  AUD_CHECK(recorder.count == 3);
  AUD_CHECK(recorder.inputBuses[0] == 1);
  AUD_CHECK(recorder.inputChannels[0] == 2);
  AUD_CHECK(recorder.times[0].input_latency_frames == kBuffer);
  AUD_CHECK(recorder.times[0].output_latency_frames == kBuffer);
  aud_io_session_destroy(session);
}

AUD_TEST(input_only_streams_use_the_capture_time) {
  AudIoSession* session = makeSession();
  Recorder recorder;
  AudIoStream* stream =
      open(session, makeConfig(recordRender, &recorder, AUD_IO_INPUT));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  const int64_t t0 = now();
  AUD_CHECK(aud_io_null_process(stream, nullptr, nullptr, 100, t0) == AUD_OK);
  AUD_CHECK(recorder.count == 1);
  AUD_CHECK(recorder.outputBuses[0] == 0);
  AUD_CHECK(recorder.inputBuses[0] == 1);
  AUD_CHECK(recorder.times[0].output_latency_frames == 0);
  AUD_CHECK(recorder.times[0].input_latency_frames == kBuffer);
  AUD_CHECK_NEAR(double(recorder.times[0].host_time_ns),
                 double(t0 - framesToNs(kBuffer)), 2);
  aud_io_session_destroy(session);
}

AUD_TEST(the_host_time_reports_its_jitter) {
  AudIoSession* session = makeSession();
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  int64_t t = now();
  process(stream, 256, t);
  t += framesToNs(256);
  process(stream, 256, t);
  AUD_CHECK(recorder.times[0].host_time_accuracy_ns == 0);  // unknown yet
  AUD_CHECK(recorder.times[1].host_time_accuracy_ns <= 1);
  t += framesToNs(256) + 100000;  // 100 µs late
  process(stream, 256, t);
  // The mean deviation moves by a sixteenth of the new deviation.
  AUD_CHECK_NEAR(double(recorder.times[2].host_time_accuracy_ns), 6250, 2);
  const AudIoCounters counters = countersOf(stream);
  AUD_CHECK_NEAR(double(counters.host_time_jitter_max_ns), 100000, 2);
  aud_io_session_destroy(session);
}

AUD_TEST(variable_callbacks_never_exceed_max_frames) {
  AudIoSession* session = makeSession();
  Recorder recorder;
  AudIoStreamConfig config = makeConfig(recordRender, &recorder);
  config.max_frames = 256;
  AudIoStream* stream = open(session, config);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  int64_t t = now();
  int64_t expected = 0;
  bool contiguous = true;
  for (uint32_t frames : {100u, 37u, 512u, 1u, 300u}) {
    const size_t first = recorder.count;
    process(stream, frames, t);
    t += framesToNs(frames);
    for (size_t i = first; i < recorder.count; ++i) {
      if (recorder.times[i].sample_position != expected) contiguous = false;
      if (recorder.times[i].frames > 256) contiguous = false;
      expected += recorder.times[i].frames;
    }
  }
  AUD_CHECK(contiguous);
  AUD_CHECK(expected == 950);
  const AudIoCounters counters = countersOf(stream);
  AUD_CHECK(counters.renders == 7);
  AUD_CHECK(counters.callback_frames_min == 1);
  AUD_CHECK(counters.callback_frames_max == 512);
  AUD_CHECK(counters.period_count == 4);
  // The late check: 512 frames came 37 frames after the 37-frame block.
  AUD_CHECK(counters.late_callbacks == 0);
  aud_io_session_destroy(session);
}

AUD_TEST(render_errors_are_reported_once) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  recorder.result = AUD_ERROR_FAILED;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  int64_t t = now();
  for (int i = 0; i < 3; ++i) {
    const std::vector<float> output = process(stream, 64, t);
    t += framesToNs(64);
    bool silent = true;
    for (float sample : output) silent = silent && sample == 0.0f;
    AUD_CHECK(silent);
  }
  AudIoNotification error{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RENDER_ERROR, &error));
  AUD_CHECK(error.code == AUD_ERROR_FAILED);
  recorder.result = AUD_OK;
  process(stream, 64, t);
  recorder.result = AUD_ERROR_FORMAT;
  process(stream, 64, t + framesToNs(64));
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RENDER_ERROR, &error));
  AUD_CHECK(error.code == AUD_ERROR_FORMAT);
  AUD_CHECK(inbox.count(AUD_IO_NOTIFY_RENDER_ERROR) == 2);
  AUD_CHECK(countersOf(stream).render_errors == 4);
  aud_io_session_destroy(session);
}

// ############################################################################
// Lifecycle

AUD_TEST(start_and_stop_follow_the_state) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_ERROR_STATE);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_ERROR_STATE);
  const int64_t t0 = now();
  process(stream, 256, t0);
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_OK);
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_STOPPED);
  AudIoNotification stopped{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STOPPED, &stopped));
  AUD_CHECK(stopped.reason == AUD_IO_REASON_REQUEST);
  // Started again a second later, the position follows the host clock.
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  process(stream, 256, t0 + 1'000'000'000);
  AUD_CHECK(recorder.count == 2);
  AUD_CHECK(recorder.times[1].sample_position == 48000);
  AUD_CHECK(inbox.count(AUD_IO_NOTIFY_STARTED) == 2);
  // The second the stream was stopped is no period between callbacks.
  const AudIoCounters counters = countersOf(stream);
  AUD_CHECK(counters.period_count == 0);
  AUD_CHECK(counters.late_callbacks == 0);
  aud_io_session_destroy(session);
}

AUD_TEST(a_format_change_holds_the_renderer) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  const int64_t t0 = now();
  process(stream, 256, t0);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_SAMPLE_RATE, 44100) ==
            AUD_OK);
  AudIoNotification notification{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_DISCONNECTED, &notification));
  AUD_CHECK(notification.reason == AUD_IO_REASON_SAMPLE_RATE);
  AUD_CHECK(notification.sample_rate == kRate);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_FORMAT_CHANGED, &notification));
  AUD_CHECK(notification.generation == 2);
  AUD_CHECK(notification.sample_rate == 44100);
  AUD_CHECK(notification.output_channels == 2);
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  AUD_CHECK(formatOf(stream).generation == 2);
  AUD_CHECK(formatOf(stream).sample_rate == 44100);
  // Held: silence, no render call, the position runs on.
  const std::vector<float> held = process(stream, 256, t0 + 500'000'000);
  bool silent = true;
  for (float sample : held) silent = silent && sample == 0.0f;
  AUD_CHECK(silent);
  AUD_CHECK(recorder.count == 1);
  AudIoCounters counters = countersOf(stream);
  AUD_CHECK(counters.held_blocks == 1);
  AUD_CHECK(counters.last_time.sample_rate == 44100);
  const int64_t heldPosition = counters.last_time.sample_position;
  AUD_CHECK(heldPosition > 256);
  AUD_CHECK(aud_io_stream_acknowledge(stream, 1) == AUD_ERROR_STATE);
  AUD_CHECK(aud_io_stream_acknowledge(stream, 3) == AUD_ERROR_STATE);
  AUD_CHECK(aud_io_stream_acknowledge(stream, 2) == AUD_OK);
  process(stream, 256, t0 + 500'000'000 + framesToNs(256, 44100));
  AUD_CHECK(recorder.count == 2);
  AUD_CHECK(recorder.times[1].sample_rate == 44100);
  AUD_CHECK(recorder.times[1].sample_position == heldPosition + 256);
  counters = countersOf(stream);
  AUD_CHECK(counters.recoveries == 1);
  AUD_CHECK(counters.disconnects == 1);
  // The held block was the first after the recovery.
  AudIoNotification started{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STARTED, &started));
  AUD_CHECK(started.value > 0);
  AUD_CHECK(started.generation == 2);
  AUD_CHECK(inbox.count(AUD_IO_NOTIFY_STARTED) == 2);
  aud_io_session_destroy(session);
}

AUD_TEST(a_renderer_that_follows_the_format_is_not_held) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AudIoStreamConfig config = makeConfig(recordRender, &recorder);
  config.flags = AUD_IO_STREAM_FOLLOW_FORMAT;
  AudIoStream* stream = open(session, config);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_SAMPLE_RATE, 44100) ==
            AUD_OK);
  AudIoNotification changed{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_FORMAT_CHANGED, &changed));
  AUD_CHECK(changed.generation == 2);
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  process(stream, 256, now());
  AUD_CHECK(recorder.count == 1);
  AUD_CHECK(recorder.times[0].sample_rate == 44100);
  AUD_CHECK(countersOf(stream).held_blocks == 0);
  AUD_CHECK(aud_io_stream_acknowledge(stream, 2) == AUD_OK);
  aud_io_session_destroy(session);
}

AUD_TEST(a_recovery_is_reported_once_the_device_runs) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  // The device opens three times without starting, then runs again.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_FAIL_START, 3) ==
            AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 0) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RECOVERED));
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  sleepMs(20);
  AUD_CHECK(inbox.count(AUD_IO_NOTIFY_RECOVERED) == 1);
  AUD_CHECK(countersOf(stream).recoveries == 1);
  // A format that changed with an open whose start failed is still
  // reported as changed.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_FAIL_START, 2) ==
            AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_SAMPLE_RATE, 96000) ==
            AUD_OK);
  AudIoNotification changed{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_FORMAT_CHANGED, &changed));
  AUD_CHECK(changed.sample_rate == 96000);
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  AUD_CHECK(inbox.count(AUD_IO_NOTIFY_RECOVERED) == 1);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_FAIL_START, -1) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

AUD_TEST(a_fixed_rate_survives_a_rate_change) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AudIoStreamConfig config = makeConfig(recordRender, &recorder);
  config.sample_rate = kRate;
  AudIoStream* stream = open(session, config);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_SAMPLE_RATE, 96000) ==
            AUD_OK);
  AudIoNotification recovered{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RECOVERED, &recovered));
  AUD_CHECK(recovered.generation == 1);
  AUD_CHECK(recovered.sample_rate == kRate);
  AUD_CHECK(inbox.count(AUD_IO_NOTIFY_FORMAT_CHANGED) == 0);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_SAMPLE_RATE, 10) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

AUD_TEST(a_recovery_moves_the_position_by_the_gap) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  const int64_t t0 = now();
  process(stream, 256, t0);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 0) == AUD_OK);
  AudIoNotification notification{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_DISCONNECTED, &notification));
  AUD_CHECK(notification.reason == AUD_IO_REASON_DEVICE_REMOVED);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RECOVERED, &notification));
  AUD_CHECK(notification.generation == 1);
  AUD_CHECK(notification.value >= 0);
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  process(stream, 256, t0 + 1'000'000'000);
  AUD_CHECK(recorder.count == 2);
  // The device was away from 256 frames to one second.
  AUD_CHECK(recorder.times[1].sample_position == 48000);
  const AudIoCounters counters = countersOf(stream);
  AUD_CHECK(counters.recoveries == 1);
  AUD_CHECK(counters.disconnects == 1);
  AUD_CHECK(counters.recovery_time_last_ns > 0);
  AUD_CHECK(counters.recovery_time_max_ns == counters.recovery_time_last_ns);
  // A gap the clock cannot see still moves the position by a frame.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 0) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RECOVERED));
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  process(stream, 256, t0 + 1'000'000'000);
  AUD_CHECK(recorder.times[2].sample_position == 48000 + 256 + 1);
  aud_io_session_destroy(session);
}

AUD_TEST(a_device_that_returns_late_is_recovered) {
  AudIoSession* session = makeSession(0);
  Inbox inbox(session);
  AudIoStream* stream =
      open(session, makeConfig(aud_io_sine_render, nullptr));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STARTED));
  // Back at once: well inside the 500 ms of lifecycle-001.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 0) == AUD_OK);
  AudIoNotification started{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STARTED, &started));
  AUD_CHECK(started.value > 0);
  AUD_CHECK(started.value < 500'000'000);
  // Away for 150 ms: the retries find it soon after it is back.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 150) ==
            AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STARTED, &started));
  AUD_CHECK(started.value >= 150'000'000);
  AUD_CHECK(started.value < 650'000'000);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, -1) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

AUD_TEST(a_recovery_that_gives_up_fails_until_started_again) {
  AudIoSession* session = makeSession(0, 100);
  Inbox inbox(session);
  AudIoStream* stream =
      open(session, makeConfig(aud_io_sine_render, nullptr));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_FAIL_OPEN, 1000000) ==
            AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 0) == AUD_OK);
  AudIoNotification failed{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_FAILED, &failed));
  AUD_CHECK(failed.code == AUD_IO_ERROR_DEVICE);
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_FAILED);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_FAIL_OPEN, 0) == AUD_OK);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RECOVERED));
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STARTED));
  // A failed stream can be stopped.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_FAIL_OPEN, 1000000) ==
            AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 0) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_FAILED));
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_OK);
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_STOPPED);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_FAIL_OPEN, -1) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

AUD_TEST(stop_during_a_recovery_ends_stopped) {
  AudIoSession* session = makeSession(0);
  Inbox inbox(session);
  AudIoStream* stream =
      open(session, makeConfig(aud_io_sine_render, nullptr));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 200) ==
            AUD_OK);
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RECOVERING));
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_OK);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);  // wanted again
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RECOVERED));
  AUD_CHECK(waitState(stream, AUD_IO_STATE_STOPPED));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_RUNNING);
  aud_io_session_destroy(session);
}

AUD_TEST(an_interruption_stops_and_resumes_the_stream) {
  AudIoSession* session = makeSession(0);
  Inbox inbox(session);
  AudIoStream* stream =
      open(session, makeConfig(aud_io_sine_render, nullptr));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STARTED));
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_INTERRUPT, 1) == AUD_OK);
  AudIoNotification notification{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_INTERRUPTED, &notification));
  AUD_CHECK(notification.reason == AUD_IO_REASON_SYSTEM);
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_INTERRUPTED);
  const uint64_t callbacks = countersOf(stream).callbacks;
  sleepMs(30);
  AUD_CHECK(countersOf(stream).callbacks == callbacks);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);  // waits for the end
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_INTERRUPTED);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_INTERRUPT, 0) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RESUMED));
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  AudIoNotification started{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_STARTED, &started));
  AUD_CHECK(started.value > 0);
  AUD_CHECK(countersOf(stream).interruptions == 1);
  // Stopped while interrupted, it stays stopped.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_INTERRUPT, 1) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_INTERRUPTED));
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_INTERRUPT, 0) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RESUMED));
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_STOPPED);
  aud_io_session_destroy(session);
}

AUD_TEST(the_client_interrupts_for_the_audio_focus) {
  AudIoSession* session = makeSession(0);
  Inbox inbox(session);
  AudIoStream* stream =
      open(session, makeConfig(aud_io_sine_render, nullptr));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_session_interrupt(session, AUD_IO_REASON_FOCUS_LOSS) ==
            AUD_OK);
  AudIoNotification notification{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_INTERRUPTED, &notification));
  AUD_CHECK(notification.reason == AUD_IO_REASON_FOCUS_LOSS);
  AUD_CHECK(aud_io_session_interrupt(session, AUD_IO_REASON_FOCUS_LOSS) ==
            AUD_OK);  // once is enough
  AUD_CHECK(aud_io_session_resume(session) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RESUMED));
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  AUD_CHECK(aud_io_session_resume(session) == AUD_OK);  // nothing to end
  AUD_CHECK(inbox.count(AUD_IO_NOTIFY_INTERRUPTED) == 1);
  aud_io_session_destroy(session);
}

AUD_TEST(a_device_lost_during_an_interruption_reopens_after_it) {
  AudIoSession* session = makeSession(0);
  Inbox inbox(session);
  AudIoStream* stream =
      open(session, makeConfig(aud_io_sine_render, nullptr));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_INTERRUPT, 1) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_INTERRUPTED));
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_CHANNELS, 1) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_DISCONNECTED));
  AUD_CHECK(aud_io_stream_state(stream) == AUD_IO_STATE_INTERRUPTED);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_INTERRUPT, 0) == AUD_OK);
  AudIoNotification changed{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_FORMAT_CHANGED, &changed));
  AUD_CHECK(changed.output_channels == 1);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_RESUMED));
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  AUD_CHECK(formatOf(stream).output_channels == 1);
  aud_io_session_destroy(session);
}

AUD_TEST(hot_plugs_list_devices_and_fall_back_to_the_default) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_HOT_PLUG, 4) == AUD_OK);
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_DEVICES_CHANGED));
  AudIoDevice devices[4] = {};
  AUD_CHECK(aud_io_session_devices(session, devices, 4) == 3);
  AUD_CHECK(std::strcmp(devices[2].id, "null:usb:1") == 0);
  AUD_CHECK(devices[2].route == AUD_IO_ROUTE_USB);
  AUD_CHECK(devices[2].directions == AUD_IO_DUPLEX);
  AUD_CHECK(devices[2].max_output_channels == 4);
  AUD_CHECK(devices[2].max_input_channels == 4);
  AudIoStreamConfig config = makeConfig(recordRender, &recorder);
  config.output_device_id = "null:usb:1";
  AudIoStream* routeChannels = open(session, config);
  AUD_CHECK(formatOf(routeChannels).output_channels == 2);  // at most 2
  config.output_channels = 4;
  AudIoStream* allChannels = open(session, config);
  AUD_CHECK(formatOf(allChannels).output_channels == 4);
  AUD_CHECK(std::strcmp(formatOf(allChannels).output_device_id,
                        "null:usb:1") == 0);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_HOT_PLUG, 0) == AUD_OK);
  // Both streams lose the interface and fall back to the default output.
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_DEVICES_CHANGED));
  AudIoNotification lost{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_DISCONNECTED, &lost));
  AUD_CHECK(lost.reason == AUD_IO_REASON_DEVICE_REMOVED);
  AUD_CHECK(inbox.waitCount(AUD_IO_NOTIFY_DISCONNECTED, 2));
  AUD_CHECK(inbox.waitCount(AUD_IO_NOTIFY_RECOVERED, 2));
  AUD_CHECK(std::strcmp(formatOf(allChannels).output_device_id, "null:out") ==
            0);
  AUD_CHECK(formatOf(allChannels).output_channels == 4);
  AUD_CHECK(aud_io_stream_state(allChannels) == AUD_IO_STATE_STOPPED);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_HOT_PLUG, 0) ==
            AUD_ERROR_STATE);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_HOT_PLUG, 33) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

AUD_TEST(a_route_change_keeps_the_format) {
  AudIoSession* session = makeSession();
  Inbox inbox(session);
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_ROUTE, 0) == AUD_OK);
  AudIoNotification routed{};
  AUD_CHECK(inbox.waitFor(AUD_IO_NOTIFY_ROUTE_CHANGED, &routed));
  AUD_CHECK(routed.reason == AUD_IO_REASON_ROUTE_OVERRIDE);
  AUD_CHECK(routed.generation == 1);
  process(stream, 64, now());
  AUD_CHECK(recorder.times[0].output_latency_frames == kBuffer + 64);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_CHANNELS, 0) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_null_inject(session, 99, 0) == AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

// ############################################################################
// Counters and notifications

AUD_TEST(a_threaded_stream_keeps_time_and_counts) {
  AudIoSession* session = makeSession(0);
  AudIoStreamConfig config = makeConfig(aud_io_sine_render, nullptr);
  config.buffer_frames = 128;
  AudIoStream* stream = open(session, config);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  sleepMs(300);
  AudIoCounters counters = countersOf(stream);
  AUD_CHECK(counters.callbacks > 50);
  AUD_CHECK(counters.callback_frames_min == 128);
  AUD_CHECK(counters.callback_frames_max == 128);
  AUD_CHECK(counters.period_count == counters.callbacks - 1);
  const double mean =
      double(counters.period_sum_ns) / double(counters.period_count);
  AUD_CHECK_NEAR(mean, 128 * 1e9 / kRate, 1e6);
  AUD_CHECK(counters.last_time.host_time_source == AUD_TIME_SOURCE_HARDWARE);
  AUD_CHECK(counters.last_time.sample_position > 0);
  // A late callback underruns.
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_LATE, 30) == AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_XRUN, 5) == AUD_OK);
  sleepMs(100);
  counters = countersOf(stream);
  AUD_CHECK(counters.late_callbacks >= 1);
  AUD_CHECK(counters.xruns >= 6);
  AUD_CHECK(aud_io_stream_reset_counters(stream) == AUD_OK);
  counters = countersOf(stream);
  AUD_CHECK(counters.xruns == 0);
  AUD_CHECK(counters.late_callbacks == 0);
  AUD_CHECK(counters.callbacks <= 2);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_LATE, 0) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_XRUN, 0) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_OK);
  aud_io_session_destroy(session);
}

AUD_TEST(block_sizes_cycle_on_the_device_thread) {
  AudIoSession* session = makeSession(0);
  const uint32_t sizes[] = {96, 160, 4096};
  AUD_CHECK(aud_io_null_set_block_sizes(session, sizes, 3) == AUD_OK);
  AudIoStreamConfig config = makeConfig(aud_io_sine_render, nullptr);
  config.max_frames = 512;
  AudIoStream* stream = open(session, config);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  sleepMs(300);
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_OK);
  const AudIoCounters counters = countersOf(stream);
  AUD_CHECK(counters.callback_frames_min == 96);
  AUD_CHECK(counters.callback_frames_max == 4096);
  AUD_CHECK(counters.renders > counters.callbacks);
  const uint32_t invalid[] = {0};
  AUD_CHECK(aud_io_null_set_block_sizes(session, invalid, 1) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_null_set_block_sizes(session, nullptr, 0) == AUD_OK);
  // The device thread has no manual clock.
  AUD_CHECK(aud_io_null_process(stream, nullptr, nullptr, 1, 0) ==
            AUD_ERROR_STATE);
  aud_io_session_destroy(session);
}

namespace {
std::atomic<int> g_wakes{0};
std::atomic<bool> g_wokeOnMain{false};
std::thread::id g_mainThread;
void countWakes(void* user) {
  g_wakes.fetch_add(1);
  if (std::this_thread::get_id() == g_mainThread) g_wokeOnMain = true;
  (void)user;
}
}  // namespace

AUD_TEST(the_listener_wakes_on_the_notification_thread) {
  g_mainThread = std::this_thread::get_id();
  AudIoSession* session = makeSession();
  AUD_CHECK(aud_io_session_set_listener(session, countWakes, nullptr) ==
            AUD_OK);
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_HOT_PLUG, 2) == AUD_OK);
  const int64_t deadline = now() + 2'000'000'000;
  while (g_wakes.load() == 0 && now() < deadline) sleepMs(1);
  AUD_CHECK(g_wakes.load() >= 1);
  AUD_CHECK(!g_wokeOnMain.load());
  AudIoNotification taken[4];
  AUD_CHECK(aud_io_session_take_notifications(session, taken, 4) == 1);
  AUD_CHECK(taken[0].type == AUD_IO_NOTIFY_DEVICES_CHANGED);
  AUD_CHECK(taken[0].stream == 0);
  AUD_CHECK(taken[0].host_time_ns > 0);
  AUD_CHECK(aud_io_session_set_listener(session, nullptr, nullptr) == AUD_OK);
  const int wakes = g_wakes.load();
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_HOT_PLUG, 2) == AUD_OK);
  sleepMs(50);
  AUD_CHECK(g_wakes.load() == wakes);
  AUD_CHECK(aud_io_session_take_notifications(session, nullptr, 1) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_session_destroy(session);
}

AUD_TEST(a_full_queue_counts_what_it_drops) {
  AudIoSession* session = makeSession(AUD_IO_SESSION_MANUAL_CLOCK, 0, 2);
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  for (int i = 0; i < 5; ++i) {
    AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_HOT_PLUG, 1) == AUD_OK);
  }
  AudIoStream* other = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(countersOf(stream).notifications_dropped == 3);
  AudIoNotification taken[8];
  AUD_CHECK(aud_io_session_take_notifications(session, taken, 8) == 2);
  AUD_CHECK(aud_io_stream_reset_counters(stream) == AUD_OK);
  AUD_CHECK(countersOf(stream).notifications_dropped == 0);
  // The reset of one stream leaves the drops another one reports.
  AUD_CHECK(countersOf(other).notifications_dropped == 3);
  aud_io_session_destroy(session);
}

AUD_TEST(notifications_keep_their_order_across_takes) {
  AudIoSession* session = makeSession();
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  process(stream, 64, now());  // STARTED from the audio thread
  AUD_CHECK(aud_io_null_inject(session, AUD_IO_FAULT_HOT_PLUG, 1) == AUD_OK);
  AUD_CHECK(aud_io_stream_stop(stream) == AUD_OK);
  AudIoNotification taken[1];
  std::vector<uint32_t> types;
  while (aud_io_session_take_notifications(session, taken, 1) == 1) {
    types.push_back(taken[0].type);
  }
  AUD_CHECK((types == std::vector<uint32_t>{AUD_IO_NOTIFY_STARTED,
                                            AUD_IO_NOTIFY_DEVICES_CHANGED,
                                            AUD_IO_NOTIFY_STOPPED}));
  aud_io_session_destroy(session);
}

AUD_TEST(closing_keeps_what_the_audio_thread_posted) {
  AudIoSession* session = makeSession();
  Recorder recorder;
  AudIoStream* stream = open(session, makeConfig(recordRender, &recorder));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  process(stream, 64, now());
  aud_io_stream_close(stream);
  AudIoNotification taken[4];
  AUD_CHECK(aud_io_session_take_notifications(session, taken, 4) == 1);
  AUD_CHECK(taken[0].type == AUD_IO_NOTIFY_STARTED);
  AUD_CHECK(taken[0].stream == 1);
  aud_io_session_destroy(session);
}

AUD_TEST(destroying_the_session_closes_its_streams) {
  AudIoSession* session = makeSession(0);
  AudIoStream* a = open(session, makeConfig(aud_io_sine_render, nullptr));
  AudIoStream* b = open(
      session, makeConfig(aud_io_thru_render, nullptr, AUD_IO_DUPLEX));
  AUD_CHECK(aud_io_stream_start(a) == AUD_OK);
  AUD_CHECK(aud_io_stream_start(b) == AUD_OK);
  AUD_CHECK(aud_io_stream_id(b) == 2);
  sleepMs(20);
  aud_io_session_destroy(session);
}

AUD_TEST(faults_under_load_keep_the_stream_alive) {
  AudIoSession* session = makeSession(0);
  Inbox inbox(session);
  AudIoStreamConfig config =
      makeConfig(aud_io_thru_render, nullptr, AUD_IO_DUPLEX);
  config.buffer_frames = 64;
  AudIoStream* stream = open(session, config);
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  const int64_t rates[] = {44100, 48000};
  for (int i = 0; i < 12; ++i) {
    switch (i % 4) {
      case 0:
        aud_io_null_inject(session, AUD_IO_FAULT_DISCONNECT, 0);
        break;
      case 1:
        aud_io_null_inject(session, AUD_IO_FAULT_SAMPLE_RATE, rates[i % 2]);
        break;
      case 2:
        aud_io_null_inject(session, AUD_IO_FAULT_INTERRUPT, 1);
        aud_io_null_inject(session, AUD_IO_FAULT_INTERRUPT, 0);
        break;
      default:
        aud_io_null_inject(session, AUD_IO_FAULT_ROUTE, 0);
        aud_io_null_inject(session, AUD_IO_FAULT_XRUN, 1);
        break;
    }
    countersOf(stream);
    formatOf(stream);
    sleepMs(15);
    inbox.pump();
    aud_io_stream_acknowledge(stream, formatOf(stream).generation);
  }
  AUD_CHECK(waitState(stream, AUD_IO_STATE_RUNNING));
  aud_io_stream_acknowledge(stream, formatOf(stream).generation);
  const uint64_t before = countersOf(stream).renders;
  sleepMs(50);
  AUD_CHECK(countersOf(stream).renders > before);
  aud_io_stream_close(stream);
  aud_io_session_destroy(session);
}

// ############################################################################
// Render functions

AUD_TEST(the_sine_follows_the_sample_position) {
  float left[480] = {};
  float right[480] = {};
  float* channels[] = {left, right};
  const AudAudioBus bus = {sizeof(AudAudioBus), 2, channels};
  AudStreamTime time{};
  time.struct_size = sizeof(AudStreamTime);
  time.sample_rate = kRate;
  time.sample_position = 48000 * 3 + 12;
  AudRenderRequest request{};
  request.struct_size = sizeof(AudRenderRequest);
  request.frames = 480;
  request.num_output_buses = 1;
  request.outputs = &bus;
  request.time = &time;
  AUD_CHECK(aud_io_sine_render(nullptr, &request) == AUD_OK);
  float peak = 0;
  for (float sample : left) peak = std::max(peak, std::fabs(sample));
  AUD_CHECK_NEAR(peak, 0.1, 0.001);
  AUD_CHECK(left[7] == right[7]);
  AUD_CHECK_NEAR(left[0], 0.1 * std::sin(6.283185307179586 * 440 * 12 / kRate),
                 1e-6);
  time.sample_rate = 0;  // an unknown rate takes 48 kHz
  AUD_CHECK(aud_io_sine_render(nullptr, &request) == AUD_OK);
  request.struct_size -= 4;
  AUD_CHECK(aud_io_sine_render(nullptr, &request) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_sine_render(nullptr, nullptr) == AUD_ERROR_INVALID_ARGUMENT);
}

AUD_TEST(the_monitor_copies_the_input) {
  float mono[4] = {1, 2, 3, 4};
  float* in[] = {mono};
  float left[4] = {};
  float right[4] = {};
  float* out[] = {left, right};
  const AudAudioBus input = {sizeof(AudAudioBus), 1, in};
  const AudAudioBus output = {sizeof(AudAudioBus), 2, out};
  AudStreamTime time{};
  time.struct_size = sizeof(AudStreamTime);
  AudRenderRequest request{};
  request.struct_size = sizeof(AudRenderRequest);
  request.frames = 4;
  request.num_input_buses = 1;
  request.inputs = &input;
  request.num_output_buses = 1;
  request.outputs = &output;
  request.time = &time;
  AUD_CHECK(aud_io_thru_render(nullptr, &request) == AUD_OK);
  AUD_CHECK(left[3] == 4 && right[3] == 4);
  request.num_input_buses = 0;
  AUD_CHECK(aud_io_thru_render(nullptr, &request) == AUD_OK);
  AUD_CHECK(left[3] == 0 && right[0] == 0);
  request.num_output_buses = 0;
  AUD_CHECK(aud_io_thru_render(nullptr, &request) == AUD_OK);
  request.time = nullptr;
  AUD_CHECK(aud_io_thru_render(nullptr, &request) ==
            AUD_ERROR_INVALID_ARGUMENT);
}

AUD_TEST(the_probe_measures_the_round_trip_of_the_loop) {
  AudIoSession* session = makeSession();
  AudIoProbe* probe = aud_io_probe_create(4800, 0);
  AUD_CHECK(probe != nullptr);
  AudIoStream* stream = open(
      session, makeConfig(aud_io_probe_render, probe, AUD_IO_DUPLEX));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  int64_t t = now();
  for (int i = 0; i < 200; ++i) {  // one second of 240-frame blocks
    std::vector<float> output(240 * 2);
    AUD_CHECK(aud_io_null_process(stream, nullptr, output.data(), 240, t) ==
              AUD_OK);
    t += framesToNs(240);
  }
  AudIoProbeResult result{};
  result.struct_size = sizeof(AudIoProbeResult);
  AUD_CHECK(aud_io_probe_read(probe, &result) == AUD_OK);
  AUD_CHECK(result.clicks == 10);
  AUD_CHECK(result.detections >= 9);
  // The null loop returns the output after exactly the reported latencies.
  AUD_CHECK(result.last_round_trip_frames == 2 * kBuffer);
  AUD_CHECK(result.min_round_trip_frames == 2 * kBuffer);
  AUD_CHECK(result.max_round_trip_frames == 2 * kBuffer);
  AUD_CHECK(result.sum_round_trip_frames ==
            result.detections * 2 * int64_t{kBuffer});
  AUD_CHECK(result.reported_round_trip_frames == 2 * kBuffer);
  // The loop returns the burst at its level.
  AUD_CHECK_NEAR(result.input_peak, 0.8, 0.01);
  aud_io_stream_close(stream);
  aud_io_probe_destroy(probe);
  AUD_CHECK(aud_io_probe_create(0, 0) == nullptr);
  AUD_CHECK(aud_io_probe_create(10, -1) == nullptr);
  AUD_CHECK(aud_io_probe_read(nullptr, &result) == AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_io_probe_render(nullptr, nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_io_probe_destroy(nullptr);
  aud_io_session_destroy(session);
}

// The RealtimeSanitizer probe of scripts/test-native.js: the render
// function allocates on the audio thread.
AUD_TEST(realtime_probe_allocates_on_the_audio_thread) {
  AudIoSession* session = makeSession();
  AudIoStream* stream =
      open(session, makeConfig(allocatingRender, nullptr));
  AUD_CHECK(aud_io_stream_start(stream) == AUD_OK);
  process(stream, 64, now());
  aud_io_session_destroy(session);
}

int main() { return aud_test::run(); }
