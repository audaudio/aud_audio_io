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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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
  Device() : serial_(nextSerial()) {}

  virtual ~Device() = default;

  // [control] Starts the callbacks.
  virtual int32_t start() = 0;

  // [control] Stops the callbacks; returns when no callback runs.
  virtual int32_t stop() = 0;

  // [control] What the device got.
  virtual const DeviceFormat& format() const = 0;

  // [control] The underruns and overruns the backend counted.
  virtual uint64_t xruns() const { return 0; }

  // [any thread] The number of the device, higher with every device opened
  // in the process. A loss report names its device by it, so that the
  // stream drops the report of a device it closed or replaced already.
  uint64_t serial() const { return serial_; }

 private:
  static uint64_t nextSerial() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
  }

  const uint64_t serial_;
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

// [any thread but the realtime thread] The device `device` of `stream` went
// away or changed; the worker of the stream recovers, unless the stream no
// longer runs on that device: one loss may be reported more than once - on
// macOS the device list, the default device and the device's presence each
// tell of an unplug, on iOS one route change follows another, on Android
// both streams of a duplex device -, and a later report would close the
// device the stream had reopened meanwhile. Takes only the worker's lock,
// so a backend may call it under its own lock - the lock that keeps the
// stream alive while its device is registered.
void streamDeviceLost(AudIoStream* stream, const Device& device,
                      uint32_t reason);

// [any thread but the realtime thread] The route of `stream` changed while
// the device stayed; the worker reports it. Takes only the worker's lock.
void streamRouteChanged(AudIoStream* stream, uint32_t reason);

// Adds to a counter only the audio thread writes; never a locked
// read-modify-write.
template <typename T>
inline void bump(std::atomic<T>& counter, T value) AUD_NONBLOCKING {
  counter.store(counter.load(std::memory_order_relaxed) + value,
                std::memory_order_relaxed);
}

template <typename T>
inline void raise(std::atomic<T>& counter, T value) AUD_NONBLOCKING {
  if (value > counter.load(std::memory_order_relaxed)) {
    counter.store(value, std::memory_order_relaxed);
  }
}

template <typename T>
inline void lower(std::atomic<T>& counter, T value) AUD_NONBLOCKING {
  if (value < counter.load(std::memory_order_relaxed)) {
    counter.store(value, std::memory_order_relaxed);
  }
}

// The fast cores of a device from the highest clock of each core in kHz
// (io-003): the cores above the slowest clock, a bit per core - the big
// cluster of two, the big and prime cores of three, where one prime core
// alone would leave the thread no room to move. A core that tells no clock
// (0, e.g. offline) is no fast core; one cluster has none.
inline uint64_t fastCoresOf(const std::vector<long>& maxClocksKhz) {
  long slowest = 0;
  for (const long khz : maxClocksKhz) {
    if (khz > 0 && (slowest == 0 || khz < slowest)) slowest = khz;
  }
  uint64_t fast = 0;
  for (size_t cpu = 0; cpu < maxClocksKhz.size() && cpu < 64; ++cpu) {
    if (maxClocksKhz[cpu] > slowest) fast |= uint64_t{1} << cpu;
  }
  return fast;
}

// Whether the affinity `now` a thread has still binds it to the fast cores
// `fast`: it allows no core outside them. The kernel leaves out the cores
// it isolates - Qualcomm's core control parks big cores while they idle -,
// so `now` may be smaller than the mask that was set; comparing them
// exactly bound the thread anew every second on the Galaxy Tab S4.
inline bool affinityHolds(uint64_t now, uint64_t fast) {
  return now != 0 && (now & ~fast) == 0;
}

// Where the audio thread of a stream runs (io-003): the device writes it,
// the counters of the stream read it. The mode and the fast cores outlive
// a reset of the counters; the counts do not.
struct Placement {
  std::atomic<uint32_t> mode{AUD_IO_PLACEMENT_SYSTEM};
  std::atomic<uint32_t> fastCpus{0};
  std::atomic<uint64_t> resets{0};
  std::atomic<uint64_t> refusals{0};
  std::atomic<uint64_t> samples{0};
  std::atomic<uint64_t> fastSamples{0};

  void resetCounts() {
    resets = 0;
    refusals = 0;
    samples = 0;
    fastSamples = 0;
  }
};

// [any thread] The placement of `stream`; it lives as long as the stream.
Placement& streamPlacement(AudIoStream* stream);

// [any thread but the realtime thread] The system took (`began`) or gave
// back the audio of every stream of the session.
void sessionInterruption(AudIoSession* session, bool began, uint32_t reason);

// [any thread but the realtime thread] Devices came or went.
void sessionDevicesChanged(AudIoSession* session);

// [any thread but the realtime thread] The microphone permission was
// answered.
void sessionPermissionChanged(AudIoSession* session, int32_t permission);

// Copies `value` into the fixed string `out` of `capacity` bytes.
void copyString(char* out, size_t capacity, const std::string& value);

// ............................................................................
// The decisions of the macOS backend
//
// What aud_io_macos.mm decides about the devices of the HAL, kept apart
// from the HAL, so that the native tests reach it on every platform. A
// device is its AudioObjectID, 0 for none; the codes are the HAL's
// four-character codes, which aud_io_macos.mm checks against CoreAudio.

namespace hal {

// The four-character code `code` of the HAL, e.g. fourCc("usb ").
constexpr uint32_t fourCc(const char (&code)[5]) {
  return uint32_t(uint8_t(code[0])) << 24 | uint32_t(uint8_t(code[1])) << 16 |
         uint32_t(uint8_t(code[2])) << 8 | uint32_t(uint8_t(code[3]));
}

// The transport types of a device (kAudioDeviceTransportType*).
constexpr uint32_t kTransportBuiltIn = fourCc("bltn");
constexpr uint32_t kTransportUsb = fourCc("usb ");
constexpr uint32_t kTransportBluetooth = fourCc("blue");
constexpr uint32_t kTransportBluetoothLe = fourCc("blea");
constexpr uint32_t kTransportHdmi = fourCc("hdmi");
constexpr uint32_t kTransportDisplayPort = fourCc("dprt");
constexpr uint32_t kTransportAirPlay = fourCc("airp");
constexpr uint32_t kTransportVirtual = fourCc("virt");
constexpr uint32_t kTransportAggregate = fourCc("grup");
constexpr uint32_t kTransportFireWire = fourCc("1394");
constexpr uint32_t kTransportPci = fourCc("pci ");
constexpr uint32_t kTransportThunderbolt = fourCc("thun");
constexpr uint32_t kTransportAvb = fourCc("eavb");

// The data source of a built-in output with headphones in the jack.
constexpr uint32_t kSourceHeadphones = fourCc("hdpn");

// The properties of a device whose change may reopen a stream.
constexpr uint32_t kNominalSampleRate = fourCc("nsrt");
constexpr uint32_t kStreamConfiguration = fourCc("slay");

// The rates a device offers from its ranges of nominal rates, each as its
// minimum and maximum: the ends of the ranges, each once and sorted, the
// first AUD_IO_MAX_SAMPLE_RATES of them.
inline std::vector<double> ratesOf(
    const std::vector<std::pair<double, double>>& ranges) {
  std::vector<double> rates;
  for (const auto& range : ranges) {
    for (const double rate : {range.first, range.second}) {
      if (std::find(rates.begin(), rates.end(), rate) == rates.end() &&
          rates.size() < AUD_IO_MAX_SAMPLE_RATES) {
        rates.push_back(rate);
      }
    }
  }
  std::sort(rates.begin(), rates.end());
  return rates;
}

// The route of a device in one direction, an AUD_IO_ROUTE_*, by its
// transport type; `source` is the data source of a built-in output, which
// tells headphones in the jack from the speaker.
inline uint32_t routeOf(uint32_t transport, bool output, uint32_t source) {
  switch (transport) {
    case kTransportBuiltIn:
      if (!output) return AUD_IO_ROUTE_BUILTIN_MIC;
      return source == kSourceHeadphones ? AUD_IO_ROUTE_WIRED_HEADPHONES
                                         : AUD_IO_ROUTE_SPEAKER;
    case kTransportUsb:
      return AUD_IO_ROUTE_USB;
    case kTransportBluetooth:
      return output ? AUD_IO_ROUTE_BLUETOOTH_A2DP : AUD_IO_ROUTE_BLUETOOTH_HFP;
    case kTransportBluetoothLe:
      return AUD_IO_ROUTE_BLUETOOTH_LE;
    case kTransportHdmi:
    case kTransportDisplayPort:
      return AUD_IO_ROUTE_HDMI;
    case kTransportAirPlay:
      return AUD_IO_ROUTE_AIRPLAY;
    case kTransportVirtual:
    case kTransportAggregate:
      return AUD_IO_ROUTE_VIRTUAL;
    case kTransportFireWire:
    case kTransportPci:
    case kTransportThunderbolt:
    case kTransportAvb:
      return AUD_IO_ROUTE_LINE;
    default:
      return AUD_IO_ROUTE_UNKNOWN;
  }
}

// Whether a stream on the devices `output` and `input`, 0 for a direction
// it lacks, lost one of them: it is missing from the devices `present`.
inline bool missingFrom(const std::vector<uint32_t>& present, uint32_t output,
                        uint32_t input) {
  for (const uint32_t device : {output, input}) {
    if (device != 0 &&
        std::find(present.begin(), present.end(), device) == present.end()) {
      return true;
    }
  }
  return false;
}

// Whether a stream reopens on `now`, the new default device of the output
// (`output`) or the input, 0 while there is none: the stream has the
// direction, named no device for it and runs on `current`, another one.
inline bool movesToDefault(bool output, const DeviceRequest& request,
                           const DeviceFormat& format, uint32_t current,
                           uint32_t now) {
  const bool follows =
      output ? format.outputChannels > 0 && request.outputId.empty()
             : format.inputChannels > 0 && request.inputId.empty();
  return follows && now != 0 && now != current;
}

// What the backend reads of a device after one of its properties changed.
struct DeviceProperties {
  bool alive = true;
  double rate = 0;       // the nominal rate; 0: unknown
  uint32_t outputs = 0;  // the channels per direction
  uint32_t inputs = 0;
};

// How a stream uses a device: for its output, its input or both, opened
// while the device ran at the nominal rate `rate`.
struct DeviceUse {
  bool output = false;
  bool input = false;
  double rate = 0;
};

// Whether a stream reopens for the nominal rate `now` of a device it opened
// at `opened`, 0 for unknown: only a stream that asked for no rate runs at
// the device's, miniaudio converts for the others. Each device counts with
// its own rate, so that the input of a duplex stream is not weighed by the
// rate of its output.
inline bool reopensForRate(const DeviceRequest& request, double opened,
                           double now) {
  return request.sampleRate == 0 && now > 0 && std::fabs(now - opened) > 0.5;
}

// Why a stream reopens after `property` of a device it uses changed, an
// AUD_IO_REASON_*, AUD_IO_REASON_NONE while it keeps running: the device
// went away or lost a direction of the stream, its rate changed as
// reopensForRate weighs it, or the channels of a stream that follows the
// hardware changed, as on iOS.
inline uint32_t lossOf(uint32_t property, const DeviceProperties& now,
                       const DeviceUse& use, const DeviceRequest& request,
                       const DeviceFormat& format) {
  if (!now.alive) return AUD_IO_REASON_DEVICE_REMOVED;
  if (property == kNominalSampleRate) {
    return reopensForRate(request, use.rate, now.rate)
               ? AUD_IO_REASON_SAMPLE_RATE
               : AUD_IO_REASON_NONE;
  }
  if (property != kStreamConfiguration) return AUD_IO_REASON_NONE;
  const bool gone =
      (use.output && now.outputs == 0) || (use.input && now.inputs == 0);
  const bool outputs =
      use.output && request.outputChannels == 0 &&
      std::min(now.outputs, kDefaultChannels) != format.outputChannels;
  const bool inputs =
      use.input && request.inputChannels == 0 &&
      std::min(now.inputs, kDefaultChannels) != format.inputChannels;
  return gone || outputs || inputs ? AUD_IO_REASON_DEVICE_REMOVED
                                   : AUD_IO_REASON_NONE;
}

}  // namespace hal

// ............................................................................
// The decisions of the iOS backend
//
// What aud_io_ios.mm decides about the routes of the AVAudioSession, kept
// apart from the session, so that the native tests reach it on every
// platform. The reasons of a route change are the session's
// AVAudioSessionRouteChangeReason values, which aud_io_ios.mm checks
// against AVFoundation.

namespace ios {

// The reasons of a route change that tell of a device.
constexpr uint64_t kNewDeviceAvailable = 1;
constexpr uint64_t kOldDeviceUnavailable = 2;

// Why the route changed, an AUD_IO_REASON_*, by the session's reason
// `change`.
inline uint32_t reasonOf(uint64_t change) {
  if (change == kOldDeviceUnavailable) return AUD_IO_REASON_DEVICE_REMOVED;
  if (change == kNewDeviceAvailable) return AUD_IO_REASON_DEVICE_ADDED;
  return AUD_IO_REASON_ROUTE_OVERRIDE;
}

// What the session tells of the route after it changed.
struct Route {
  double rate = 0;       // the hardware's rate; 0: unknown
  uint32_t outputs = 0;  // the channels per direction
  uint32_t inputs = 0;
  std::string output;  // the UID of the first port per direction
  std::string input;
};

// Why a device reopens after the route changed for `why`, an
// AUD_IO_REASON_*, AUD_IO_REASON_NONE while it keeps running: a stream
// that follows the hardware reopens for a new rate, as on macOS, or a new
// channel count, one with input on the new input; iOS moves the output
// itself, so the output reopens only once the old one is gone.
inline uint32_t lossOf(uint32_t why, const Route& now,
                       const DeviceRequest& request,
                       const DeviceFormat& format) {
  const bool output = format.outputChannels > 0;
  const bool input = format.inputChannels > 0;
  const bool rate = hal::reopensForRate(request, format.sampleRate, now.rate);
  const bool outputs =
      output && request.outputChannels == 0 &&
      std::min(now.outputs, kDefaultChannels) != format.outputChannels;
  const bool inputs =
      input && request.inputChannels == 0 &&
      std::min(now.inputs, kDefaultChannels) != format.inputChannels;
  const bool moved = (input && format.inputId != now.input) ||
                     (output && format.outputId != now.output &&
                      why == AUD_IO_REASON_DEVICE_REMOVED);
  return rate || outputs || inputs || moved ? why : AUD_IO_REASON_NONE;
}

}  // namespace ios

}  // namespace aud_io

#endif  // AUD_IO_INTERNAL_HPP
