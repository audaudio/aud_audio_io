// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// What the stream and the backends of aud_audio_io share: the device and
// backend interfaces each platform implements, and the functions through
// which a device reaches its stream. The stream itself lives in
// aud_io_stream.cpp; a backend only sees it as an opaque AudIoStream.

#ifndef AUD_IO_INTERNAL_HPP
#define AUD_IO_INTERNAL_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "aud_audio_io.h"

// With AUD_IO_RTSAN the stream callbacks carry Clang's nonblocking effect,
// so that the RealtimeSanitizer checks everything a callback calls, the
// render function included (ticket 21); other toolchains see nothing.
#if AUD_IO_RTSAN && defined(__clang__)
#define AUD_NONBLOCKING [[clang::nonblocking]]
#else
#define AUD_NONBLOCKING
#endif

namespace aud_io {

// The defaults of AudIoStreamConfig and AudIoSessionConfig.
constexpr uint32_t kDefaultMaxFrames = 1024;
constexpr uint32_t kMaxMaxFrames = 16384;
constexpr uint32_t kDefaultNotificationCapacity = 256;
// A stream that asks for 0 channels gets the route's, at most this many.
constexpr uint32_t kDefaultChannels = 2;

// What a device reports with one callback. Host times are in the clock of
// aud_clock.h; 0 means unknown.
struct DeviceTime {
  int64_t callbackNs = 0;  // when the callback began
  int64_t outputNs = 0;    // when the first output frame reaches the output
  int64_t inputNs = 0;     // when the first input frame was captured
  uint32_t outputLatencyFrames = 0;
  uint32_t inputLatencyFrames = 0;
  uint32_t source = AUD_TIME_SOURCE_NONE;  // of outputNs and inputNs
};

// What a device got from the backend.
struct DeviceFormat {
  double sampleRate = 0;
  uint32_t outputChannels = 0;
  uint32_t inputChannels = 0;
  uint32_t bufferFrames = 0;
  uint32_t burstFrames = 0;
  uint32_t performanceMode = AUD_IO_PERFORMANCE_LOW_LATENCY;
  bool exclusive = false;
  uint32_t timeSource = AUD_TIME_SOURCE_NONE;
  std::string outputId;
  std::string inputId;
  std::string backend;
};

// What a stream asks a backend for: its configuration with the device ids
// copied.
struct DeviceRequest {
  uint32_t direction = AUD_IO_OUTPUT;
  std::string outputId;  // empty: the default output
  std::string inputId;   // empty: the default input
  uint32_t outputChannels = 0;  // 0: the route's channels
  uint32_t inputChannels = 0;
  double sampleRate = 0;  // 0: the device's rate
  uint32_t bufferFrames = 0;
  uint32_t maxFrames = kDefaultMaxFrames;
  uint32_t performanceMode = AUD_IO_PERFORMANCE_LOW_LATENCY;
  uint32_t flags = 0;
};

// An open device of a stream. The destructor stops and closes it; no
// callback runs afterwards.
class Device {
 public:
  virtual ~Device() = default;

  // [control] Starts the callbacks.
  virtual int32_t start() = 0;

  // [control] Stops the callbacks; returns when no callback runs.
  virtual int32_t stop() = 0;

  // [control] What the device got.
  virtual const DeviceFormat& format() const = 0;

  // [control] The underruns and overruns the backend counted.
  virtual uint64_t xruns() const { return 0; }
};

// The devices of a platform. One backend per session.
class Backend {
 public:
  virtual ~Backend() = default;

  // [control] The name of the backend, e.g. "oboe".
  virtual const char* name() const = 0;

  // [control] Lists the devices; AUD_ERROR_UNSUPPORTED where Dart lists them.
  virtual int32_t devices(std::vector<AudIoDevice>& out) = 0;

  // [control] The microphone permission, an AUD_IO_PERMISSION_*.
  virtual int32_t permission() = 0;

  // [control] Asks for the microphone permission; the answer arrives as a
  // notification.
  virtual int32_t requestPermission() = 0;

  // [control] Makes the platform's audio session ready for a stream to
  // start, e.g. activates the AVAudioSession; AUD_IO_ERROR_INTERRUPTED while
  // the system holds the audio.
  virtual int32_t activate() { return AUD_OK; }

  // [control] Opens a device for `stream`; NULL with the reason in `result`.
  virtual std::unique_ptr<Device> open(const DeviceRequest& request,
                                       AudIoStream* stream,
                                       int32_t* result) = 0;

  // [control] Null backend: injects a fault.
  virtual int32_t inject(uint32_t fault, int64_t value) {
    return AUD_ERROR_UNSUPPORTED;
  }

  // [control] Null backend: the cycle of callback sizes.
  virtual int32_t setBlockSizes(const uint32_t* sizes, uint32_t count) {
    return AUD_ERROR_UNSUPPORTED;
  }

  // [control] Null backend with the manual clock: runs one callback of the
  // device of `stream`.
  virtual int32_t process(AudIoStream* stream, const float* input,
                          float* output, uint32_t frames, int64_t hostTimeNs) {
    return AUD_ERROR_UNSUPPORTED;
  }
};

// The backends. The platform file of the target defines
// createPlatformBackend; on platforms without devices yet it returns the null
// backend.
std::unique_ptr<Backend> createNullBackend(AudIoSession* session,
                                           const AudIoSessionConfig& config);
std::unique_ptr<Backend> createPlatformBackend(
    AudIoSession* session, const AudIoSessionConfig& config);

// ............................................................................
// What a device and a backend call on the stream and the session

// [realtime] One device callback of `stream`: interleaved input and output,
// either NULL when the stream has no such direction, `frames` frames each.
void streamProcess(AudIoStream* stream, const float* input, float* output,
                   uint32_t frames, const DeviceTime& time) AUD_NONBLOCKING;

// [any thread but the realtime thread] The device of `stream` went away or
// changed; the worker of the stream recovers. Takes only the worker's lock,
// so a backend may call it under its own lock - the lock that keeps the
// stream alive while its device is registered.
void streamDeviceLost(AudIoStream* stream, uint32_t reason);

// [any thread but the realtime thread] The route of `stream` changed while
// the device stayed; the worker reports it. Takes only the worker's lock.
void streamRouteChanged(AudIoStream* stream, uint32_t reason);

// [any thread but the realtime thread] The system took (`began`) or gave
// back the audio of every stream of the session.
void sessionInterruption(AudIoSession* session, bool began, uint32_t reason);

// [any thread but the realtime thread] Devices came or went.
void sessionDevicesChanged(AudIoSession* session);

// [any thread but the realtime thread] The microphone permission was
// answered.
void sessionPermissionChanged(AudIoSession* session, int32_t permission);

// [any thread but the realtime thread] Reports every stream of the session
// as lost, e.g. after the media services of iOS were reset.
void sessionAllDevicesLost(AudIoSession* session, uint32_t reason);

// Copies `value` into the fixed string `out` of `capacity` bytes.
void copyString(char* out, size_t capacity, const std::string& value);

}  // namespace aud_io

#endif  // AUD_IO_INTERNAL_HPP
