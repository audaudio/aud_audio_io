// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The C API of the audio device IO of aud_audio_io (ticket 21, S3).
//
// A session owns the platform's audio session (AVAudioSession on iOS),
// lists the devices and wakes a listener when devices or streams change. A
// stream plays and records on devices of its session (io-001, io-002) and
// calls an AudRenderFunction of aud_abi.h from its audio thread:
//
// - The device buffers are de-interleaved into preallocated planar buses.
//   Device callbacks larger than the stream's max_frames are split into
//   blocks of at most max_frames, so a renderer prepared for max_frames
//   never sees a larger block.
// - Every block carries an AudStreamTime (time-001): its sample position,
//   the host time at which its first frame reaches the output (an input-only
//   stream: at which it was captured) with source and accuracy, and the
//   latency of each direction.
// - Nothing in the callback allocates, locks, logs or calls into Dart
//   (interop-001). The stream counts in atomics and posts notifications into
//   a lock-free queue; a notification thread wakes the listener and the
//   control thread takes the notifications.
// - The stream's worker thread recovers from disconnects, interruptions,
//   route and sample-rate changes on its own (lifecycle-001). Each case
//   reaches the control thread as a notification, so that the client runs
//   the route-change sequence of its renderer. After a change of the sample
//   rate or the channel counts the stream holds the render function - it
//   plays silence and advances the sample position - until the client
//   acknowledges the new format with aud_io_stream_acknowledge.
// - The sample position runs on across a recovery by the frames the device
//   was away, at least one, so that the time filters of the engine see the
//   discontinuity and reset.
//
// Backends: Oboe on Android, miniaudio with AVAudioSession on iOS, and the
// null backend on every platform - a device that keeps time, with faults
// that tests inject. macOS, Windows and Linux get their devices with the
// desktop backends (S3b to S3d); until then their platform backend is the
// null backend.
//
// Threads: [control] functions are called from one thread, the control
// thread; [realtime] from the audio thread.

#ifndef AUD_AUDIO_IO_H
#define AUD_AUDIO_IO_H

#include <stdint.h>

#include "aud_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// The version of this API; it moves with every change of a struct or a
// function below.
#define AUD_IO_API_VERSION 2

// Capacities of the fixed strings and lists of the structs below.
#define AUD_IO_MAX_ID 128
#define AUD_IO_MAX_NAME 128
#define AUD_IO_MAX_SAMPLE_RATES 16
#define AUD_IO_MAX_CHANNELS 32

// Results beyond the result codes of aud_abi.h.
enum {
  // The microphone permission is denied or not yet asked for.
  AUD_IO_ERROR_PERMISSION = -100,
  // No device with the requested id, or no device of the direction.
  AUD_IO_ERROR_NO_DEVICE = -101,
  // The backend refused the device or the format.
  AUD_IO_ERROR_DEVICE = -102,
  // The system holds the audio, e.g. during a phone call.
  AUD_IO_ERROR_INTERRUPTED = -103,
};

// Backends of a session.
enum {
  // The platform's backend: Oboe on Android, miniaudio with AVAudioSession
  // on iOS, the null backend elsewhere until S3b to S3d.
  AUD_IO_BACKEND_PLATFORM = 0,
  // A device that keeps time and takes injected faults, for tests.
  AUD_IO_BACKEND_NULL = 1,
};

// Directions of a device or a stream.
enum {
  AUD_IO_OUTPUT = 1 << 0,
  AUD_IO_INPUT = 1 << 1,
  AUD_IO_DUPLEX = AUD_IO_OUTPUT | AUD_IO_INPUT,
};

// The kind of route a device belongs to.
enum {
  AUD_IO_ROUTE_UNKNOWN = 0,
  AUD_IO_ROUTE_SPEAKER = 1,
  AUD_IO_ROUTE_RECEIVER = 2,  // the earpiece
  AUD_IO_ROUTE_BUILTIN_MIC = 3,
  AUD_IO_ROUTE_WIRED_HEADPHONES = 4,
  AUD_IO_ROUTE_WIRED_HEADSET = 5,  // headphones with a microphone
  AUD_IO_ROUTE_LINE = 6,
  AUD_IO_ROUTE_USB = 7,
  AUD_IO_ROUTE_BLUETOOTH_A2DP = 8,
  AUD_IO_ROUTE_BLUETOOTH_HFP = 9,
  AUD_IO_ROUTE_BLUETOOTH_LE = 10,
  AUD_IO_ROUTE_HDMI = 11,
  AUD_IO_ROUTE_AIRPLAY = 12,
  AUD_IO_ROUTE_CAR = 13,
  AUD_IO_ROUTE_HEARING_AID = 14,
  AUD_IO_ROUTE_VIRTUAL = 15,  // the null device
};

// Flags of a device.
enum {
  AUD_IO_DEVICE_DEFAULT_OUTPUT = 1 << 0,
  AUD_IO_DEVICE_DEFAULT_INPUT = 1 << 1,
  // The device belongs to the current route (iOS) or is in use.
  AUD_IO_DEVICE_ACTIVE = 1 << 2,
};

// An audio device. Ids are stable while the device is attached: the device
// id of AudioManager on Android, the port UID on iOS.
typedef struct AudIoDevice {
  uint32_t struct_size;
  uint32_t directions;  // AUD_IO_OUTPUT and AUD_IO_INPUT
  uint32_t route;       // AUD_IO_ROUTE_*
  uint32_t flags;       // AUD_IO_DEVICE_*
  uint32_t max_output_channels;
  uint32_t max_input_channels;
  uint32_t num_sample_rates;  // 0: any rate the backend converts to
  uint32_t reserved;
  double sample_rates[AUD_IO_MAX_SAMPLE_RATES];
  char id[AUD_IO_MAX_ID];
  char name[AUD_IO_MAX_NAME];
} AudIoDevice;

// Flags of a session.
enum {
  // iOS: mix with the audio of other apps instead of interrupting it.
  AUD_IO_SESSION_MIX_WITH_OTHERS = 1 << 0,
  // iOS: allow Bluetooth hands-free input, which drops the rate to 16 kHz;
  // without it Bluetooth plays through A2DP and records from the device.
  AUD_IO_SESSION_BLUETOOTH_HFP = 1 << 1,
  // iOS: the measurement mode, without any processing of the input.
  AUD_IO_SESSION_MEASUREMENT = 1 << 2,
  // Null backend: no device thread; the test drives every callback with
  // aud_io_null_process.
  AUD_IO_SESSION_MANUAL_CLOCK = 1 << 8,
};

typedef struct AudIoSessionConfig {
  uint32_t struct_size;
  uint32_t backend;     // AUD_IO_BACKEND_*
  uint32_t directions;  // what the app uses; iOS sets the category by it
  uint32_t flags;       // AUD_IO_SESSION_*
  uint32_t notification_capacity;  // 0 = 256
  // A recovery that has not reopened the device after this long gives up
  // and reports AUD_IO_NOTIFY_FAILED; 0 = 5000.
  uint32_t recovery_timeout_ms;
} AudIoSessionConfig;

// The microphone permission.
enum {
  AUD_IO_PERMISSION_UNDETERMINED = 0,
  AUD_IO_PERMISSION_DENIED = 1,
  AUD_IO_PERMISSION_GRANTED = 2,
};

// Performance modes of a stream.
enum {
  AUD_IO_PERFORMANCE_LOW_LATENCY = 0,
  AUD_IO_PERFORMANCE_NONE = 1,
  AUD_IO_PERFORMANCE_POWER_SAVING = 2,
};

// Flags of a stream.
enum {
  // Android: ask for exclusive sharing, the MMAP path with the lowest
  // latency; the backend falls back to shared mode.
  AUD_IO_STREAM_EXCLUSIVE = 1 << 0,
  // Android: let Oboe's latency tuner grow the output buffer from one burst
  // until the underruns stop.
  AUD_IO_STREAM_LATENCY_TUNER = 1 << 1,
  // The render function follows a new rate or channel count on its own,
  // like the render functions of this package: the stream does not hold it
  // after a format change, and no acknowledge is needed.
  AUD_IO_STREAM_FOLLOW_FORMAT = 1 << 2,
};

// What a stream asks for. A field left 0 takes the default.
typedef struct AudIoStreamConfig {
  uint32_t struct_size;
  uint32_t direction;  // AUD_IO_OUTPUT, AUD_IO_INPUT or AUD_IO_DUPLEX
  const char* output_device_id;  // NULL or "": the default output
  const char* input_device_id;   // NULL or "": the default input
  uint32_t output_channels;  // 0: the route's channels, at most 2
  uint32_t input_channels;   // 0: the route's channels, at most 2
  // 0: the device's rate, followed across route changes; a fixed rate is
  // converted by the backend when the device runs at another one.
  double sample_rate;
  uint32_t buffer_frames;  // the device buffer; 0: the low-latency default
  uint32_t max_frames;     // the largest render block; 0 = 1024
  uint32_t performance_mode;  // AUD_IO_PERFORMANCE_*
  uint32_t flags;             // AUD_IO_STREAM_*
  AudRenderFunction render;   // [realtime] called with every block
  void* render_user;          // handed to render, e.g. an AudGraph
} AudIoStreamConfig;

// States of a stream.
enum {
  AUD_IO_STATE_STOPPED = 0,
  AUD_IO_STATE_RUNNING = 1,
  // The worker reopens the device after a disconnect or a change.
  AUD_IO_STATE_RECOVERING = 2,
  // The system holds the audio; the stream resumes when it is given back.
  AUD_IO_STATE_INTERRUPTED = 3,
  // The recovery gave up; aud_io_stream_start tries again.
  AUD_IO_STATE_FAILED = 4,
};

// What a stream got. Changes with every notification that names a new
// generation.
typedef struct AudIoStreamFormat {
  uint32_t struct_size;
  uint32_t direction;
  double sample_rate;
  uint32_t output_channels;
  uint32_t input_channels;
  uint32_t max_frames;     // the largest render block
  uint32_t buffer_frames;  // the device buffer
  uint32_t burst_frames;   // the device's period (Android: the burst)
  uint32_t generation;     // grows with every change of rate or channels
  uint32_t performance_mode;  // the granted AUD_IO_PERFORMANCE_*
  uint32_t exclusive;         // 1: exclusive sharing was granted
  uint32_t time_source;       // the AUD_TIME_SOURCE_* of the host times
  uint32_t reserved;
  char output_device_id[AUD_IO_MAX_ID];
  char input_device_id[AUD_IO_MAX_ID];
  char backend[64];  // e.g. "oboe/aaudio", "miniaudio/Core Audio", "null"
} AudIoStreamFormat;

// The counters of a stream since it opened or since the last reset.
typedef struct AudIoCounters {
  uint32_t struct_size;
  uint32_t state;              // AUD_IO_STATE_*
  uint64_t callbacks;          // device callbacks
  uint64_t frames;             // frames of the device callbacks
  uint64_t renders;            // render calls, after splitting
  uint32_t callback_frames_min;  // the smallest device callback
  uint32_t callback_frames_max;  // the largest device callback
  int64_t period_min_ns;  // between the starts of two callbacks
  int64_t period_max_ns;
  int64_t period_sum_ns;
  uint64_t period_count;
  uint64_t late_callbacks;  // a period above 1.5 times the last block
  uint64_t xruns;           // underruns and overruns of the backend
  uint64_t disconnects;
  uint64_t recoveries;      // the device reopened after a loss or a change
  uint64_t interruptions;
  uint64_t held_blocks;     // silent while the client acknowledges
  uint64_t render_errors;   // the render function returned an error
  uint64_t notifications_dropped;
  int64_t callback_time_max_ns;  // spent inside the callback
  int64_t callback_time_sum_ns;
  int64_t recovery_time_max_ns;   // from the loss to the first callback
  int64_t recovery_time_last_ns;
  int64_t host_time_jitter_max_ns;  // the largest timestamp deviation
  AudStreamTime last_time;  // the time of the last block
} AudIoCounters;

// Notifications of a session and its streams.
enum {
  // Devices came or went; list them again.
  AUD_IO_NOTIFY_DEVICES_CHANGED = 1,
  // The first callback ran after a start or a recovery; `value` is the time
  // from the loss of the device to this callback in nanoseconds, 0 after a
  // plain start.
  AUD_IO_NOTIFY_STARTED = 2,
  // The stream stopped on request.
  AUD_IO_NOTIFY_STOPPED = 3,
  // The device went away or changed; the stream recovers (`reason`).
  AUD_IO_NOTIFY_DISCONNECTED = 4,
  // The route changed and the format stayed; latencies may differ.
  AUD_IO_NOTIFY_ROUTE_CHANGED = 5,
  // The stream reopened with a new sample rate or channel count, the format
  // of `generation`; it holds the render function until the client
  // acknowledges, unless the stream was opened with
  // AUD_IO_STREAM_FOLLOW_FORMAT.
  AUD_IO_NOTIFY_FORMAT_CHANGED = 6,
  // The system took the audio (`reason`).
  AUD_IO_NOTIFY_INTERRUPTED = 7,
  // The interruption ended and the stream runs again.
  AUD_IO_NOTIFY_RESUMED = 8,
  // The recovery gave up (`code`).
  AUD_IO_NOTIFY_FAILED = 9,
  // The microphone permission was answered (`code`: AUD_IO_PERMISSION_*).
  AUD_IO_NOTIFY_PERMISSION = 10,
  // The render function returned an error (`code`); reported once until a
  // block succeeds again.
  AUD_IO_NOTIFY_RENDER_ERROR = 11,
  // The stream reopened with the same format.
  AUD_IO_NOTIFY_RECOVERED = 12,
};

// Why something happened.
enum {
  AUD_IO_REASON_NONE = 0,
  AUD_IO_REASON_DEVICE_REMOVED = 1,
  AUD_IO_REASON_DEVICE_ADDED = 2,
  AUD_IO_REASON_ROUTE_OVERRIDE = 3,  // a category change or an override
  AUD_IO_REASON_SYSTEM = 4,          // a call, an alarm, another app
  AUD_IO_REASON_APP_SUSPENDED = 5,   // iOS suspended the app
  AUD_IO_REASON_FOCUS_LOSS = 6,      // Android: the audio focus went
  AUD_IO_REASON_MEDIA_SERVICES_RESET = 7,
  AUD_IO_REASON_SAMPLE_RATE = 8,
  AUD_IO_REASON_ERROR = 9,           // the backend reported an error
  AUD_IO_REASON_BUILTIN_MIC_MUTED = 10,
  AUD_IO_REASON_REQUEST = 11,        // the client asked for it
};

typedef struct AudIoNotification {
  uint32_t struct_size;
  uint32_t type;        // AUD_IO_NOTIFY_*
  int32_t stream;       // the stream id; 0 for the session
  int32_t code;         // a result code or AUD_IO_PERMISSION_*
  uint32_t reason;      // AUD_IO_REASON_*
  uint32_t generation;  // the stream's format generation afterwards
  int64_t host_time_ns;  // when it happened
  int64_t value;         // see the notification type
  double sample_rate;    // the stream's rate afterwards
  uint32_t output_channels;
  uint32_t input_channels;
} AudIoNotification;

// Wakes the control thread: notifications wait. Called on the notification
// thread, never on the realtime thread.
typedef void (*AudIoListener)(void* user);

typedef struct AudIoSession AudIoSession;
typedef struct AudIoStream AudIoStream;

// ............................................................................
// Session

// [control] Creates a session; NULL for an invalid configuration. On iOS the
// session configures and activates the AVAudioSession.
AUD_EXPORT AudIoSession* aud_io_session_create(
    const AudIoSessionConfig* config);

// [control] Closes the remaining streams and destroys the session.
AUD_EXPORT void aud_io_session_destroy(AudIoSession* session);

// [control] The name of the session's backend, e.g. "oboe" or "null".
AUD_EXPORT const char* aud_io_session_backend_name(AudIoSession* session);

// [control] Writes up to `capacity` devices into `out` and returns how many
// exist; AUD_ERROR_UNSUPPORTED where the Dart side lists them (Android).
AUD_EXPORT int32_t aud_io_session_devices(AudIoSession* session,
                                          AudIoDevice* out,
                                          uint32_t capacity);

// [control] The microphone permission, an AUD_IO_PERMISSION_*;
// AUD_ERROR_UNSUPPORTED where the Dart side asks (Android).
AUD_EXPORT int32_t aud_io_session_permission(AudIoSession* session);

// [control] Asks the user for the microphone permission; the answer arrives
// as AUD_IO_NOTIFY_PERMISSION. AUD_ERROR_UNSUPPORTED where the Dart side
// asks.
AUD_EXPORT int32_t aud_io_session_request_permission(AudIoSession* session);

// [control] Sets the listener the notification thread wakes; NULL removes
// it. The notification thread has stopped calling the old listener when
// the function returns.
AUD_EXPORT int32_t aud_io_session_set_listener(AudIoSession* session,
                                               AudIoListener listener,
                                               void* user);

// [control] Takes the waiting notifications of the session and its
// streams, at most `capacity`, in the order they happened; returns the
// number taken.
AUD_EXPORT int32_t aud_io_session_take_notifications(AudIoSession* session,
                                                     AudIoNotification* out,
                                                     uint32_t capacity);

// [control] Tells the streams of the session that the system took the
// audio, e.g. Android's audio focus went; running streams stop and report
// AUD_IO_NOTIFY_INTERRUPTED.
AUD_EXPORT int32_t aud_io_session_interrupt(AudIoSession* session,
                                            uint32_t reason);

// [control] Ends an interruption: streams that ran before start again and
// report AUD_IO_NOTIFY_RESUMED.
AUD_EXPORT int32_t aud_io_session_resume(AudIoSession* session);

// ............................................................................
// Streams

// [control] Opens a stream in the stopped state; NULL with the reason in
// `result` (may be NULL), e.g. AUD_IO_ERROR_PERMISSION.
AUD_EXPORT AudIoStream* aud_io_stream_open(AudIoSession* session,
                                           const AudIoStreamConfig* config,
                                           int32_t* result);

// [control] Stops and closes the stream; no callback runs afterwards.
AUD_EXPORT void aud_io_stream_close(AudIoStream* stream);

// [control] Starts the callbacks; from the failed state it reopens first.
AUD_EXPORT int32_t aud_io_stream_start(AudIoStream* stream);

// [control] Stops the callbacks; returns when no callback runs.
AUD_EXPORT int32_t aud_io_stream_stop(AudIoStream* stream);

// [control] The AUD_IO_STATE_* of the stream.
AUD_EXPORT int32_t aud_io_stream_state(AudIoStream* stream);

// [control] The id that names the stream in notifications.
AUD_EXPORT int32_t aud_io_stream_id(AudIoStream* stream);

// [control] Writes the format the stream got into `out`.
AUD_EXPORT int32_t aud_io_stream_format(AudIoStream* stream,
                                        AudIoStreamFormat* out);

// [control] Tells the stream that the render function is prepared for the
// format of `generation`; the stream calls it again from the next block.
// AUD_ERROR_STATE for a generation the stream does not have.
AUD_EXPORT int32_t aud_io_stream_acknowledge(AudIoStream* stream,
                                             uint32_t generation);

// [control] Writes the counters into `out`.
AUD_EXPORT int32_t aud_io_stream_counters(AudIoStream* stream,
                                          AudIoCounters* out);

// [control] Zeroes the counters; the next period starts fresh.
AUD_EXPORT int32_t aud_io_stream_reset_counters(AudIoStream* stream);

// ............................................................................
// Render functions of the package

// [realtime] Renders a 440 Hz sine at -20 dBFS into every output channel:
// the smoke signal of the package. The phase follows the sample position,
// so the function keeps no state; `user` is ignored.
AUD_EXPORT int32_t aud_io_sine_render(void* user,
                                      const AudRenderRequest* request);

// [realtime] Copies the input into the output, channel by channel: a
// monitor. The last input channel feeds the outputs beyond it, so a mono
// input plays on every output channel. `user` is ignored.
AUD_EXPORT int32_t aud_io_thru_render(void* user,
                                      const AudRenderRequest* request);

// The latency probe: plays a click - a 5 ms burst of 2 kHz - every interval
// on every output channel and measures the time until its onset on the
// first input channel, through a loop from the output to the input (a cable
// or the air).
typedef struct AudIoProbe AudIoProbe;

// What a probe measured.
typedef struct AudIoProbeResult {
  uint32_t struct_size;
  // The largest absolute input sample so far: whether the input hears
  // anything, and how loud the clicks come back.
  float input_peak;
  int64_t clicks;      // clicks played
  int64_t detections;  // onsets found within an interval
  int64_t last_round_trip_frames;  // from the last click to its onset
  int64_t min_round_trip_frames;
  int64_t max_round_trip_frames;
  int64_t sum_round_trip_frames;
  // The output plus the input latency the stream reported with the last
  // detected click.
  int64_t reported_round_trip_frames;
} AudIoProbeResult;

// [control] Creates a probe that clicks every `interval_frames` frames and
// detects an onset above `threshold` (0 = 0.1); NULL for an interval of 0.
AUD_EXPORT AudIoProbe* aud_io_probe_create(uint32_t interval_frames,
                                           float threshold);

// [control] Destroys a probe no stream renders with any more.
AUD_EXPORT void aud_io_probe_destroy(AudIoProbe* probe);

// [realtime] Renders the probe in `user`, an AudIoProbe.
AUD_EXPORT int32_t aud_io_probe_render(void* user,
                                       const AudRenderRequest* request);

// [control] Writes what the probe measured into `out`.
AUD_EXPORT int32_t aud_io_probe_read(AudIoProbe* probe,
                                     AudIoProbeResult* out);

// ............................................................................
// Null backend: faults for tests

enum {
  // The device goes away; `value` milliseconds later it is back.
  AUD_IO_FAULT_DISCONNECT = 1,
  // The device switches to the rate `value` in Hz and goes away briefly.
  AUD_IO_FAULT_SAMPLE_RATE = 2,
  // The system takes the audio (`value` 1) or gives it back (`value` 0).
  AUD_IO_FAULT_INTERRUPT = 3,
  // The next callback comes `value` milliseconds late and underruns.
  AUD_IO_FAULT_LATE = 4,
  // The backend reports `value` underruns.
  AUD_IO_FAULT_XRUN = 5,
  // A device with `value` channels is plugged in (`value` > 0) or the last
  // plugged one is removed (`value` 0).
  AUD_IO_FAULT_HOT_PLUG = 6,
  // The next `value` opens of a device fail.
  AUD_IO_FAULT_FAIL_OPEN = 7,
  // The microphone permission becomes `value`, an AUD_IO_PERMISSION_*.
  AUD_IO_FAULT_PERMISSION = 8,
  // The device's route changes; the format stays.
  AUD_IO_FAULT_ROUTE = 9,
  // The channel count of the default output becomes `value`; the device
  // goes away briefly.
  AUD_IO_FAULT_CHANNELS = 10,
  // The next `value` starts of a device fail although it opens.
  AUD_IO_FAULT_FAIL_START = 11,
};

// [control] Injects a fault into the null devices of the session.
AUD_EXPORT int32_t aud_io_null_inject(AudIoSession* session, uint32_t fault,
                                      int64_t value);

// [control] Lets the null devices of the session call back in the cycle of
// `sizes` instead of their buffer size; 0 sizes restore it.
AUD_EXPORT int32_t aud_io_null_set_block_sizes(AudIoSession* session,
                                               const uint32_t* sizes,
                                               uint32_t count);

// [control] With AUD_IO_SESSION_MANUAL_CLOCK: runs one device callback of
// the stream with interleaved `input` and `output` (either may be NULL) at
// the host time `host_time_ns`; AUD_ERROR_STATE unless the stream runs.
AUD_EXPORT int32_t aud_io_null_process(AudIoStream* stream,
                                       const float* input, float* output,
                                       uint32_t frames, int64_t host_time_ns);

#ifdef __cplusplus
}
#endif

#endif  // AUD_AUDIO_IO_H
