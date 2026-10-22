// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The macOS backend (io-001, the macOS part of S3b): miniaudio's Core Audio
// device on the devices of the HAL, which this file lists and observes. A
// stream names its devices by their UID; one without an id opens the
// current default device and reopens on the next one when the default
// changes, as a stream follows the route on iOS. miniaudio is always given
// the UID, so that it never switches devices behind the stream's back. The
// HAL's listeners - the device list, the defaults, and per open device its
// presence, rate, channels, data source and overloads - run on a queue of
// the backend and reach the streams through streamDeviceLost and
// streamRouteChanged. miniaudio's callback carries no timestamp, so the host
// times are estimates as on iOS: the callback time plus the block of the
// callback and the device's latency, safety offset and stream latency,
// read on the control thread and handed to the audio thread through
// atomics (time-001). What the backend decides about the devices - their
// rates and routes, and when a stream reopens - lives in hal:: of
// aud_io_internal.hpp, where the native tests reach it. Objective-C++ with
// ARC.

#include <TargetConditionals.h>

#if defined(__APPLE__) && TARGET_OS_OSX

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_COREAUDIO
#define MA_NO_RUNTIME_LINKING
#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio/miniaudio.h"

#import <AVFoundation/AVFoundation.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "aud_clock.h"
#include "aud_io_internal.hpp"

namespace aud_io {
namespace {

constexpr double kFallbackRate = 48000;
// The low-latency default of the IO buffer: 256 frames at 48 kHz, 5.3 ms.
constexpr double kDefaultBufferSeconds = 256.0 / 48000.0;
constexpr const char* kBackendName = "miniaudio/Core Audio";

// hal:: names the devices and the codes of the HAL by plain integers.
static_assert(std::is_same<AudioObjectID, uint32_t>::value);
static_assert(kAudioObjectUnknown == 0);
static_assert(hal::kTransportBuiltIn == kAudioDeviceTransportTypeBuiltIn);
static_assert(hal::kTransportUsb == kAudioDeviceTransportTypeUSB);
static_assert(hal::kTransportBluetooth == kAudioDeviceTransportTypeBluetooth);
static_assert(hal::kTransportBluetoothLe ==
              kAudioDeviceTransportTypeBluetoothLE);
static_assert(hal::kTransportHdmi == kAudioDeviceTransportTypeHDMI);
static_assert(hal::kTransportDisplayPort ==
              kAudioDeviceTransportTypeDisplayPort);
static_assert(hal::kTransportAirPlay == kAudioDeviceTransportTypeAirPlay);
static_assert(hal::kTransportVirtual == kAudioDeviceTransportTypeVirtual);
static_assert(hal::kTransportAggregate == kAudioDeviceTransportTypeAggregate);
static_assert(hal::kTransportFireWire == kAudioDeviceTransportTypeFireWire);
static_assert(hal::kTransportPci == kAudioDeviceTransportTypePCI);
static_assert(hal::kTransportThunderbolt ==
              kAudioDeviceTransportTypeThunderbolt);
static_assert(hal::kTransportAvb == kAudioDeviceTransportTypeAVB);
static_assert(hal::kNominalSampleRate ==
              kAudioDevicePropertyNominalSampleRate);
static_assert(hal::kStreamConfiguration ==
              kAudioDevicePropertyStreamConfiguration);

// .............................................................................
// The HAL

AudioObjectPropertyAddress addressOf(AudioObjectPropertySelector selector,
                                     AudioObjectPropertyScope scope =
                                         kAudioObjectPropertyScopeGlobal) {
  return {selector, scope, kAudioObjectPropertyElementMain};
}

AudioObjectPropertyScope scopeOf(bool output) {
  return output ? kAudioObjectPropertyScopeOutput
                : kAudioObjectPropertyScopeInput;
}

template <typename T>
bool readProperty(AudioObjectID object, AudioObjectPropertySelector selector,
                  AudioObjectPropertyScope scope, T* out) {
  const AudioObjectPropertyAddress address = addressOf(selector, scope);
  UInt32 size = sizeof(T);
  return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size,
                                    out) == noErr;
}

std::string stringProperty(AudioObjectID object,
                           AudioObjectPropertySelector selector) {
  CFStringRef value = nullptr;
  if (!readProperty(object, selector, kAudioObjectPropertyScopeGlobal,
                    &value) ||
      value == nullptr) {
    return std::string();
  }
  char buffer[AUD_IO_MAX_NAME * 2] = {0};
  CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8);
  CFRelease(value);
  return std::string(buffer);
}

std::string uidOf(AudioObjectID device) {
  return stringProperty(device, kAudioDevicePropertyDeviceUID);
}

std::vector<AudioObjectID> allDevices() {
  const AudioObjectPropertyAddress address =
      addressOf(kAudioHardwarePropertyDevices);
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0,
                                     nullptr, &size) != noErr) {
    return {};
  }
  std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0,
                                 nullptr, &size, devices.data()) != noErr) {
    return {};
  }
  devices.resize(size / sizeof(AudioObjectID));
  return devices;
}

AudioObjectID defaultDevice(bool output) {
  AudioObjectID device = kAudioObjectUnknown;
  readProperty(kAudioObjectSystemObject,
               output ? kAudioHardwarePropertyDefaultOutputDevice
                      : kAudioHardwarePropertyDefaultInputDevice,
               kAudioObjectPropertyScopeGlobal, &device);
  return device;
}

AudioObjectID deviceWithUid(const std::string& uid) {
  for (AudioObjectID device : allDevices()) {
    if (uidOf(device) == uid) return device;
  }
  return kAudioObjectUnknown;
}

uint32_t channelsOf(AudioObjectID device, bool output) {
  const AudioObjectPropertyAddress address =
      addressOf(kAudioDevicePropertyStreamConfiguration, scopeOf(output));
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) !=
          noErr ||
      size == 0) {
    return 0;
  }
  std::vector<uint8_t> storage(size);
  auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
  if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, list) !=
      noErr) {
    return 0;
  }
  uint32_t channels = 0;
  for (UInt32 i = 0; i < list->mNumberBuffers; ++i) {
    channels += list->mBuffers[i].mNumberChannels;
  }
  return channels;
}

double nominalRateOf(AudioObjectID device) {
  Float64 rate = 0;
  readProperty(device, kAudioDevicePropertyNominalSampleRate,
               kAudioObjectPropertyScopeGlobal, &rate);
  return rate;
}

// The rates the device offers, as hal::ratesOf picks them from its ranges.
std::vector<double> ratesOf(AudioObjectID device) {
  const AudioObjectPropertyAddress address =
      addressOf(kAudioDevicePropertyAvailableNominalSampleRates);
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) !=
      noErr) {
    return {};
  }
  std::vector<AudioValueRange> ranges(size / sizeof(AudioValueRange));
  if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size,
                                 ranges.data()) != noErr) {
    return {};
  }
  ranges.resize(size / sizeof(AudioValueRange));
  std::vector<std::pair<double, double>> ends;
  for (const AudioValueRange& range : ranges) {
    ends.emplace_back(range.mMinimum, range.mMaximum);
  }
  return hal::ratesOf(ends);
}

// The frames between the callback and the hardware in one direction: the
// device's latency, its safety offset and the latency of its first stream.
uint32_t latencyFramesOf(AudioObjectID device, bool output) {
  const AudioObjectPropertyScope scope = scopeOf(output);
  UInt32 latency = 0;
  UInt32 safety = 0;
  readProperty(device, kAudioDevicePropertyLatency, scope, &latency);
  readProperty(device, kAudioDevicePropertySafetyOffset, scope, &safety);
  UInt32 streamLatency = 0;
  const AudioObjectPropertyAddress address =
      addressOf(kAudioDevicePropertyStreams, scope);
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) ==
          noErr &&
      size >= sizeof(AudioStreamID)) {
    std::vector<AudioStreamID> streams(size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size,
                                   streams.data()) == noErr) {
      readProperty(streams[0], kAudioStreamPropertyLatency,
                   kAudioObjectPropertyScopeGlobal, &streamLatency);
    }
  }
  return latency + safety + streamLatency;
}

uint32_t bufferFramesOf(AudioObjectID device) {
  UInt32 frames = 0;
  readProperty(device, kAudioDevicePropertyBufferFrameSize,
               kAudioObjectPropertyScopeGlobal, &frames);
  return frames;
}

uint32_t routeOf(AudioObjectID device, bool output) {
  UInt32 transport = 0;
  readProperty(device, kAudioDevicePropertyTransportType,
               kAudioObjectPropertyScopeGlobal, &transport);
  // Only a built-in output tells headphones in the jack by its data source.
  UInt32 source = 0;
  if (transport == kAudioDeviceTransportTypeBuiltIn && output) {
    readProperty(device, kAudioDevicePropertyDataSource,
                 kAudioObjectPropertyScopeOutput, &source);
  }
  return hal::routeOf(transport, output, source);
}

bool isHidden(AudioObjectID device) {
  UInt32 hidden = 0;
  readProperty(device, kAudioDevicePropertyIsHidden,
               kAudioObjectPropertyScopeGlobal, &hidden);
  return hidden != 0;
}

// The device of a direction: the one with `uid`, the default without one.
AudioObjectID resolve(const std::string& uid, bool output) {
  return uid.empty() ? defaultDevice(output) : deviceWithUid(uid);
}

class MacBackend;

// What the HAL's listener blocks and the permission's answer reach the
// backend through: the destructor clears it under the lock, so a block that
// runs late finds no backend, and one that runs meanwhile is waited for.
struct Guard {
  std::mutex mutex;
  MacBackend* backend = nullptr;
};

class MacDevice : public Device {
 public:
  MacDevice(MacBackend* backend, AudIoStream* stream,
            const DeviceRequest& request)
      : backend_(backend), stream_(stream), request_(request) {}

  ~MacDevice() override;

  // [control] Opens the miniaudio device on `output` and `input`, either
  // kAudioObjectUnknown when the stream has no such direction.
  int32_t open(ma_context* context, AudioObjectID output, AudioObjectID input);

  int32_t start() override {
    if (ma_device_start(&device_) != MA_SUCCESS) return AUD_IO_ERROR_DEVICE;
    return AUD_OK;
  }

  int32_t stop() override {
    if (ma_device_is_started(&device_)) ma_device_stop(&device_);
    return AUD_OK;
  }

  const DeviceFormat& format() const override { return format_; }

  uint64_t xruns() const override {
    return xruns_.load(std::memory_order_relaxed);
  }

  AudIoStream* stream() const { return stream_; }
  const DeviceRequest& request() const { return request_; }
  AudioObjectID output() const { return output_; }
  AudioObjectID input() const { return input_; }

  // How the device uses the HAL's device `id`; neither direction when it
  // does not.
  hal::DeviceUse useOf(AudioObjectID id) const {
    hal::DeviceUse use;
    use.output = id == output_;
    use.input = id == input_;
    use.rate = use.output ? outputRate_ : inputRate_;
    return use;
  }

  // [under the backend's mutex_] Reports the loss `reason` to the stream,
  // which drops it once it no longer runs on this device: one loss reaches
  // the backend more than once.
  void lost(uint32_t reason) { streamDeviceLost(stream_, *this, reason); }

  // [control] Reads the latencies of the devices for the audio thread.
  void readLatencies() {
    if (output_ != kAudioObjectUnknown) {
      const double rate = nominalRateOf(output_);
      outputNs_.store(
          rate > 0 ? int64_t(latencyFramesOf(output_, true) / rate * 1e9) : 0,
          std::memory_order_relaxed);
    }
    if (input_ != kAudioObjectUnknown) {
      const double rate = nominalRateOf(input_);
      inputNs_.store(
          rate > 0 ? int64_t(latencyFramesOf(input_, false) / rate * 1e9) : 0,
          std::memory_order_relaxed);
    }
  }

  // [any thread] The HAL reported an overload of one of the devices.
  void overloaded() { xruns_.fetch_add(1, std::memory_order_relaxed); }

  // [realtime] One callback of miniaudio.
  void process(void* output, const void* input,
               ma_uint32 frames) AUD_NONBLOCKING {
    DeviceTime time;
    time.callbackNs = aud_clock_now_ns();
    time.source = AUD_TIME_SOURCE_ESTIMATED;
    // The block plays once the one before it has played, and was captured
    // while the one before it was handed over.
    const double framesPerNs = format_.sampleRate / 1e9;
    const int64_t block = int64_t(std::llround(frames / framesPerNs));
    if (format_.outputChannels > 0) {
      const int64_t latency =
          block + outputNs_.load(std::memory_order_relaxed);
      time.outputNs = time.callbackNs + latency;
      time.outputLatencyFrames = uint32_t(std::llround(latency * framesPerNs));
    }
    if (format_.inputChannels > 0) {
      // A duplex device hands the input over through miniaudio's ring
      // buffer: the block waited there as long as the frames queued behind
      // it.
      const int64_t queued =
          format_.outputChannels > 0
              ? int64_t(std::llround(
                    ma_pcm_rb_available_read(&device_.duplexRB.rb) /
                    framesPerNs))
              : 0;
      const int64_t latency =
          block + queued + inputNs_.load(std::memory_order_relaxed);
      time.inputNs = time.callbackNs - latency;
      time.inputLatencyFrames = uint32_t(std::llround(latency * framesPerNs));
    }
    streamProcess(stream_, static_cast<const float*>(input),
                  static_cast<float*>(output), frames, time);
  }

 private:
  MacBackend* backend_;
  AudIoStream* stream_;
  DeviceRequest request_;
  DeviceFormat format_;
  ma_device device_{};
  bool initialized_ = false;
  AudioObjectID output_ = kAudioObjectUnknown;
  AudioObjectID input_ = kAudioObjectUnknown;
  // The nominal rates of the devices when they opened.
  double outputRate_ = 0;
  double inputRate_ = 0;
  std::atomic<int64_t> outputNs_{0};
  std::atomic<int64_t> inputNs_{0};
  std::atomic<uint64_t> xruns_{0};
};

void dataCallback(ma_device* device, void* output, const void* input,
                  ma_uint32 frames) AUD_NONBLOCKING {
  static_cast<MacDevice*>(device->pUserData)->process(output, input, frames);
}

// The properties the backend observes on every device a stream uses.
constexpr AudioObjectPropertySelector kDeviceSelectors[] = {
    kAudioDevicePropertyDeviceIsAlive,
    kAudioDevicePropertyNominalSampleRate,
    kAudioDevicePropertyStreamConfiguration,
    kAudioDevicePropertyDataSource,
    kAudioDeviceProcessorOverload,
};

class MacBackend : public Backend {
 public:
  MacBackend(AudIoSession* session, const AudIoSessionConfig& config)
      : session_(session),
        guard_(std::make_shared<Guard>()),
        queue_(dispatch_queue_create("aud_audio_io.hal",
                                     DISPATCH_QUEUE_SERIAL)) {
    guard_->backend = this;
  }

  ~MacBackend() override {
    {
      std::lock_guard<std::mutex> lock(guard_->mutex);
      guard_->backend = nullptr;
    }
    for (Listener& listener : systemListeners_) remove(listener);
    systemListeners_.clear();
    for (auto& entry : watched_) {
      for (Listener& listener : entry.second.listeners) remove(listener);
    }
    watched_.clear();
    if (contextReady_) ma_context_uninit(&context_);
  }

  // The session that hears the permission's answer.
  AudIoSession* session() const { return session_; }

  // [control] Starts miniaudio and observes the HAL.
  bool init() {
    ma_context_config contextConfig = ma_context_config_init();
    const ma_backend backends[] = {ma_backend_coreaudio};
    if (ma_context_init(backends, 1, &contextConfig, &context_) !=
        MA_SUCCESS) {
      return false;
    }
    contextReady_ = true;
    for (AudioObjectPropertySelector selector :
         {kAudioHardwarePropertyDevices,
          kAudioHardwarePropertyDefaultOutputDevice,
          kAudioHardwarePropertyDefaultInputDevice}) {
      systemListeners_.push_back(listen(kAudioObjectSystemObject, selector));
    }
    return true;
  }

  const char* name() const override { return kBackendName; }

  int32_t devices(std::vector<AudIoDevice>& out) override {
    const AudioObjectID defaultOutput = defaultDevice(true);
    const AudioObjectID defaultInput = defaultDevice(false);
    for (AudioObjectID id : allDevices()) {
      if (isHidden(id)) continue;
      const uint32_t outputs = channelsOf(id, true);
      const uint32_t inputs = channelsOf(id, false);
      if (outputs == 0 && inputs == 0) continue;
      AudIoDevice device{};
      device.struct_size = sizeof(AudIoDevice);
      device.directions =
          (outputs > 0 ? AUD_IO_OUTPUT : 0) | (inputs > 0 ? AUD_IO_INPUT : 0);
      device.route = routeOf(id, outputs > 0);
      if (id == defaultOutput && outputs > 0) {
        device.flags |= AUD_IO_DEVICE_DEFAULT_OUTPUT | AUD_IO_DEVICE_ACTIVE;
      }
      if (id == defaultInput && inputs > 0) {
        device.flags |= AUD_IO_DEVICE_DEFAULT_INPUT | AUD_IO_DEVICE_ACTIVE;
      }
      if (inUse(id)) device.flags |= AUD_IO_DEVICE_ACTIVE;
      device.max_output_channels = outputs;
      device.max_input_channels = inputs;
      const std::vector<double> rates = ratesOf(id);
      device.num_sample_rates = uint32_t(rates.size());
      std::copy(rates.begin(), rates.end(), device.sample_rates);
      copyString(device.id, sizeof(device.id), uidOf(id));
      copyString(device.name, sizeof(device.name),
                 stringProperty(id, kAudioObjectPropertyName));
      out.push_back(device);
    }
    return AUD_OK;
  }

  int32_t permission() override {
    if (@available(macOS 14.0, *)) {
      switch ([AVAudioApplication sharedInstance].recordPermission) {
        case AVAudioApplicationRecordPermissionGranted:
          return AUD_IO_PERMISSION_GRANTED;
        case AVAudioApplicationRecordPermissionDenied:
          return AUD_IO_PERMISSION_DENIED;
        default:
          return AUD_IO_PERMISSION_UNDETERMINED;
      }
    }
    // Before macOS 14 the microphone's authorization of AVCaptureDevice.
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
      case AVAuthorizationStatusAuthorized:
        return AUD_IO_PERMISSION_GRANTED;
      case AVAuthorizationStatusDenied:
      case AVAuthorizationStatusRestricted:
        return AUD_IO_PERMISSION_DENIED;
      default:
        return AUD_IO_PERMISSION_UNDETERMINED;
    }
  }

  int32_t requestPermission() override {
    // The user may answer after the session is gone.
    std::shared_ptr<Guard> guard = guard_;
    void (^answer)(BOOL) = ^(BOOL granted) {
      std::lock_guard<std::mutex> lock(guard->mutex);
      if (guard->backend == nullptr) return;
      sessionPermissionChanged(guard->backend->session(),
                               granted ? AUD_IO_PERMISSION_GRANTED
                                       : AUD_IO_PERMISSION_DENIED);
    };
    if (@available(macOS 14.0, *)) {
      [AVAudioApplication requestRecordPermissionWithCompletionHandler:answer];
      return AUD_OK;
    }
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:answer];
    return AUD_OK;
  }

  std::unique_ptr<Device> open(const DeviceRequest& request,
                               AudIoStream* stream,
                               int32_t* result) override {
    const bool output = (request.direction & AUD_IO_OUTPUT) != 0;
    const bool input = (request.direction & AUD_IO_INPUT) != 0;
    if (input && permission() == AUD_IO_PERMISSION_DENIED) {
      *result = AUD_IO_ERROR_PERMISSION;
      return nullptr;
    }
    const AudioObjectID outputId =
        output ? resolve(request.outputId, true) : kAudioObjectUnknown;
    const AudioObjectID inputId =
        input ? resolve(request.inputId, false) : kAudioObjectUnknown;
    if ((output && (outputId == kAudioObjectUnknown ||
                    channelsOf(outputId, true) == 0)) ||
        (input && (inputId == kAudioObjectUnknown ||
                   channelsOf(inputId, false) == 0))) {
      *result = AUD_IO_ERROR_NO_DEVICE;
      return nullptr;
    }
    auto device = std::make_unique<MacDevice>(this, stream, request);
    *result = device->open(&context_, outputId, inputId);
    if (*result != AUD_OK) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.push_back(device.get());
    for (AudioObjectID id : {outputId, inputId}) {
      if (id != kAudioObjectUnknown) watch(id);
    }
    return device;
  }

  // [control] A device closes.
  void remove(MacDevice* device) {
    std::vector<Listener> unused;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = std::find(devices_.begin(), devices_.end(), device);
      // A device whose open failed was never registered nor watched.
      if (it == devices_.end()) return;
      devices_.erase(it);
      for (AudioObjectID id : {device->output(), device->input()}) {
        if (id != kAudioObjectUnknown) unwatch(id, unused);
      }
    }
    // Outside the lock, which a listener block may be waiting for.
    for (Listener& listener : unused) remove(listener);
  }

  // [HAL queue] A property of `object` changed.
  void changed(AudioObjectID object, AudioObjectPropertySelector selector) {
    if (object == kAudioObjectSystemObject) {
      systemChanged(selector);
    } else {
      deviceChanged(object, selector);
    }
  }

 private:
  struct Listener {
    AudioObjectID object = kAudioObjectUnknown;
    AudioObjectPropertyAddress address{};
    AudioObjectPropertyListenerBlock block = nil;
  };

  struct Watch {
    int users = 0;
    std::vector<Listener> listeners;
  };

  // Adds a listener for `selector` of `object` that reaches the backend
  // through the guard.
  Listener listen(AudioObjectID object, AudioObjectPropertySelector selector) {
    std::shared_ptr<Guard> guard = guard_;
    Listener listener;
    listener.object = object;
    listener.address = addressOf(selector, kAudioObjectPropertyScopeWildcard);
    listener.block = ^(UInt32, const AudioObjectPropertyAddress*) {
      std::lock_guard<std::mutex> lock(guard->mutex);
      if (guard->backend == nullptr) return;
      guard->backend->changed(object, selector);
    };
    AudioObjectAddPropertyListenerBlock(object, &listener.address, queue_,
                                        listener.block);
    return listener;
  }

  void remove(Listener& listener) {
    AudioObjectRemovePropertyListenerBlock(listener.object, &listener.address,
                                           queue_, listener.block);
    listener.block = nil;
  }

  // [under mutex_] Observes `device` while a stream uses it.
  void watch(AudioObjectID device) {
    Watch& watch = watched_[device];
    if (watch.users++ > 0) return;
    for (AudioObjectPropertySelector selector : kDeviceSelectors) {
      watch.listeners.push_back(listen(device, selector));
    }
  }

  // [under mutex_] Stops observing `device` once no stream uses it: moves
  // its listeners to `unused`, which the caller removes outside the lock.
  void unwatch(AudioObjectID device, std::vector<Listener>& unused) {
    auto it = watched_.find(device);
    if (it == watched_.end() || --it->second.users > 0) return;
    for (Listener& listener : it->second.listeners) {
      unused.push_back(listener);
    }
    watched_.erase(it);
  }

  bool inUse(AudioObjectID device) {
    std::lock_guard<std::mutex> lock(mutex_);
    return watched_.count(device) > 0;
  }

  // [HAL queue] Devices came or went, or a default changed.
  void systemChanged(AudioObjectPropertySelector selector) {
    const bool list = selector == kAudioHardwarePropertyDevices;
    const bool output = selector == kAudioHardwarePropertyDefaultOutputDevice;
    const AudioObjectID now =
        list ? kAudioObjectUnknown : defaultDevice(output);
    const std::vector<AudioObjectID> present =
        list ? allDevices() : std::vector<AudioObjectID>();
    {
      // Under the lock: a stream lives at least as long as its device is
      // registered, and the stream functions take no lock of the stream's
      // control.
      std::lock_guard<std::mutex> lock(mutex_);
      for (MacDevice* device : devices_) {
        if (list) {
          if (hal::missingFrom(present, device->output(), device->input())) {
            device->lost(AUD_IO_REASON_DEVICE_REMOVED);
          }
          continue;
        }
        // A stream without an id follows the default device.
        if (hal::movesToDefault(output, device->request(), device->format(),
                                output ? device->output() : device->input(),
                                now)) {
          device->lost(AUD_IO_REASON_ROUTE_OVERRIDE);
        }
      }
    }
    sessionDevicesChanged(session_);
  }

  // [HAL queue] A property of a device a stream uses changed.
  void deviceChanged(AudioObjectID id, AudioObjectPropertySelector selector) {
    hal::DeviceProperties now;
    UInt32 alive = 1;
    readProperty(id, kAudioDevicePropertyDeviceIsAlive,
                 kAudioObjectPropertyScopeGlobal, &alive);
    now.alive = alive != 0;
    now.rate = nominalRateOf(id);
    now.outputs = channelsOf(id, true);
    now.inputs = channelsOf(id, false);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (MacDevice* device : devices_) {
        const hal::DeviceUse use = device->useOf(id);
        if (!use.output && !use.input) continue;
        if (selector == kAudioDeviceProcessorOverload) {
          device->overloaded();
          continue;
        }
        device->readLatencies();
        const uint32_t lost = hal::lossOf(selector, now, use, device->request(),
                                          device->format());
        if (lost != AUD_IO_REASON_NONE) {
          device->lost(lost);
        } else if (selector == kAudioDevicePropertyDataSource) {
          // Headphones in the built-in jack: the device stays, the route
          // changes.
          streamRouteChanged(device->stream(), AUD_IO_REASON_ROUTE_OVERRIDE);
        }
      }
    }
    if (selector == kAudioDevicePropertyDataSource ||
        selector == kAudioDevicePropertyStreamConfiguration ||
        selector == kAudioDevicePropertyDeviceIsAlive) {
      sessionDevicesChanged(session_);
    }
  }

  AudIoSession* session_;
  ma_context context_{};
  bool contextReady_ = false;
  std::shared_ptr<Guard> guard_;
  dispatch_queue_t queue_;
  std::vector<Listener> systemListeners_;
  std::mutex mutex_;
  std::vector<MacDevice*> devices_;
  std::map<AudioObjectID, Watch> watched_;
};

MacDevice::~MacDevice() {
  backend_->remove(this);
  if (initialized_) ma_device_uninit(&device_);
}

int32_t MacDevice::open(ma_context* context, AudioObjectID output,
                        AudioObjectID input) {
  output_ = output;
  input_ = input;
  const bool hasOutput = output != kAudioObjectUnknown;
  const bool hasInput = input != kAudioObjectUnknown;
  ma_device_config config = ma_device_config_init(
      hasOutput && hasInput ? ma_device_type_duplex
      : hasOutput           ? ma_device_type_playback
                            : ma_device_type_capture);
  // The UIDs, also of the default devices: miniaudio follows a default
  // device of its own accord when it gets none, and the stream reopens
  // instead.
  ma_device_id outputId{};
  ma_device_id inputId{};
  if (hasOutput) {
    copyString(outputId.coreaudio, sizeof(outputId.coreaudio), uidOf(output));
    config.playback.pDeviceID = &outputId;
  }
  if (hasInput) {
    copyString(inputId.coreaudio, sizeof(inputId.coreaudio), uidOf(input));
    config.capture.pDeviceID = &inputId;
  }
  config.playback.format = ma_format_f32;
  config.playback.channels =
      request_.outputChannels > 0
          ? request_.outputChannels
          : std::min(channelsOf(output, true), kDefaultChannels);
  config.capture.format = ma_format_f32;
  config.capture.channels =
      request_.inputChannels > 0
          ? request_.inputChannels
          : std::max(1u, std::min(channelsOf(input, false), kDefaultChannels));
  // The device's own rate unless the stream asks for one; miniaudio
  // converts then, it never changes the device's rate. The rates of both
  // devices tell later whether their own rate changed (hal::lossOf).
  outputRate_ = hasOutput ? nominalRateOf(output) : 0;
  inputRate_ = hasInput ? nominalRateOf(input) : 0;
  const double hardware = hasOutput ? outputRate_ : inputRate_;
  const double rate = request_.sampleRate > 0 ? request_.sampleRate
                      : hardware > 0          ? hardware
                                              : kFallbackRate;
  config.sampleRate = ma_uint32(std::lround(rate));
  config.periodSizeInFrames =
      request_.bufferFrames > 0
          ? request_.bufferFrames
          : ma_uint32(std::lround(rate * kDefaultBufferSeconds));
  config.performanceProfile = request_.performanceMode ==
                                      AUD_IO_PERFORMANCE_POWER_SAVING
                                  ? ma_performance_profile_conservative
                                  : ma_performance_profile_low_latency;
  config.coreaudio.allowNominalSampleRateChange = MA_FALSE;
  // Callbacks of the size the hardware asks for; the stream splits them.
  config.noFixedSizedCallback = MA_TRUE;
  config.dataCallback = dataCallback;
  config.pUserData = this;
  if (ma_device_init(context, &config, &device_) != MA_SUCCESS) {
    return AUD_IO_ERROR_DEVICE;
  }
  initialized_ = true;
  format_.sampleRate = device_.sampleRate;
  format_.outputChannels = hasOutput ? device_.playback.channels : 0;
  format_.inputChannels = hasInput ? device_.capture.channels : 0;
  // What the HAL granted, not what miniaudio asked for.
  format_.bufferFrames = bufferFramesOf(hasOutput ? output : input);
  format_.burstFrames = hasOutput ? device_.playback.internalPeriodSizeInFrames
                                  : device_.capture.internalPeriodSizeInFrames;
  format_.performanceMode = request_.performanceMode;
  format_.exclusive = false;
  format_.timeSource = AUD_TIME_SOURCE_ESTIMATED;
  if (hasOutput) format_.outputId = uidOf(output);
  if (hasInput) format_.inputId = uidOf(input);
  format_.backend = kBackendName;
  readLatencies();
  return AUD_OK;
}

}  // namespace

std::unique_ptr<Backend> createPlatformBackend(
    AudIoSession* session, const AudIoSessionConfig& config) {
  auto backend = std::make_unique<MacBackend>(session, config);
  if (!backend->init()) return nullptr;
  return backend;
}

}  // namespace aud_io

#endif  // defined(__APPLE__) && TARGET_OS_OSX
