// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The iOS backend (io-001): miniaudio's Core Audio device on the
// AVAudioSession this file configures. The session lists the routes and
// inputs, follows route changes, interruptions, the reset of the media
// services and the return of the app to the foreground (lifecycle-001),
// and asks for the microphone permission. miniaudio's callback carries no
// timestamp, so the host times are estimates: the callback time plus the
// block of the callback and the latency the session reports for each
// direction, read on the control thread and handed to the audio thread
// through atomics (time-001, ticket 21 decision 2: a RemoteIO path follows
// in S15 if the measured error exceeds 1 ms). The block of the callback,
// not the session's IO buffer: the simulator calls back with 512 frames
// while it reports a buffer of 256. Objective-C++ with ARC.

#include <TargetConditionals.h>

#if TARGET_OS_IPHONE

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
#import <UIKit/UIKit.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aud_clock.h"
#include "aud_io_internal.hpp"

namespace aud_io {
namespace {

constexpr double kFallbackRate = 48000;
// The low-latency default of the IO buffer: 256 frames at 48 kHz, 5.3 ms.
constexpr double kDefaultBufferSeconds = 256.0 / 48000.0;

std::string stringOf(NSString* value) {
  return value == nil ? std::string() : std::string(value.UTF8String);
}

uint32_t routeOf(NSString* portType) {
  if ([portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]) {
    return AUD_IO_ROUTE_SPEAKER;
  }
  if ([portType isEqualToString:AVAudioSessionPortBuiltInReceiver]) {
    return AUD_IO_ROUTE_RECEIVER;
  }
  if ([portType isEqualToString:AVAudioSessionPortBuiltInMic]) {
    return AUD_IO_ROUTE_BUILTIN_MIC;
  }
  if ([portType isEqualToString:AVAudioSessionPortHeadphones]) {
    return AUD_IO_ROUTE_WIRED_HEADPHONES;
  }
  if ([portType isEqualToString:AVAudioSessionPortHeadsetMic]) {
    return AUD_IO_ROUTE_WIRED_HEADSET;
  }
  if ([portType isEqualToString:AVAudioSessionPortLineIn] ||
      [portType isEqualToString:AVAudioSessionPortLineOut]) {
    return AUD_IO_ROUTE_LINE;
  }
  if ([portType isEqualToString:AVAudioSessionPortUSBAudio]) {
    return AUD_IO_ROUTE_USB;
  }
  if ([portType isEqualToString:AVAudioSessionPortBluetoothA2DP]) {
    return AUD_IO_ROUTE_BLUETOOTH_A2DP;
  }
  if ([portType isEqualToString:AVAudioSessionPortBluetoothHFP]) {
    return AUD_IO_ROUTE_BLUETOOTH_HFP;
  }
  if ([portType isEqualToString:AVAudioSessionPortBluetoothLE]) {
    return AUD_IO_ROUTE_BLUETOOTH_LE;
  }
  if ([portType isEqualToString:AVAudioSessionPortHDMI]) {
    return AUD_IO_ROUTE_HDMI;
  }
  if ([portType isEqualToString:AVAudioSessionPortAirPlay]) {
    return AUD_IO_ROUTE_AIRPLAY;
  }
  if ([portType isEqualToString:AVAudioSessionPortCarAudio]) {
    return AUD_IO_ROUTE_CAR;
  }
  return AUD_IO_ROUTE_UNKNOWN;
}

AudIoDevice deviceOf(AVAudioSessionPortDescription* port, uint32_t direction,
                     uint32_t flags, double sampleRate) {
  AudIoDevice device{};
  device.struct_size = sizeof(AudIoDevice);
  device.directions = direction;
  device.route = routeOf(port.portType);
  device.flags = flags;
  const uint32_t channels = uint32_t(port.channels.count);
  device.max_output_channels = direction == AUD_IO_OUTPUT ? channels : 0;
  device.max_input_channels = direction == AUD_IO_INPUT ? channels : 0;
  device.num_sample_rates = sampleRate > 0 ? 1 : 0;
  device.sample_rates[0] = sampleRate;
  copyString(device.id, sizeof(device.id), stringOf(port.UID));
  copyString(device.name, sizeof(device.name), stringOf(port.portName));
  return device;
}

// The UID of the first port of the current route in one direction.
std::string currentPort(bool output) {
  AVAudioSessionRouteDescription* route =
      [AVAudioSession sharedInstance].currentRoute;
  NSArray<AVAudioSessionPortDescription*>* ports =
      output ? route.outputs : route.inputs;
  return ports.count > 0 ? stringOf(ports.firstObject.UID) : std::string();
}

class IosBackend;

// What the blocks of the notification center and of the permission request
// reach the backend through: the destructor clears it under the lock, so a
// block that runs late finds no backend, and one that runs meanwhile is
// waited for.
struct Guard {
  std::mutex mutex;
  IosBackend* backend = nullptr;
};

class IosDevice : public Device {
 public:
  IosDevice(IosBackend* backend, AudIoStream* stream,
            const DeviceRequest& request)
      : backend_(backend), stream_(stream), request_(request) {}

  ~IosDevice() override;

  // [control] Opens the miniaudio device on the backend's context.
  int32_t open(ma_context* context);

  int32_t start() override {
    if (ma_device_start(&device_) != MA_SUCCESS) return AUD_IO_ERROR_DEVICE;
    return AUD_OK;
  }

  int32_t stop() override {
    if (ma_device_is_started(&device_)) ma_device_stop(&device_);
    return AUD_OK;
  }

  const DeviceFormat& format() const override { return format_; }

  AudIoStream* stream() const { return stream_; }
  const DeviceRequest& request() const { return request_; }

  // [control] Reads the latencies of the session for the audio thread.
  void readLatencies() {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    outputNs_.store(int64_t(session.outputLatency * 1e9),
                    std::memory_order_relaxed);
    inputNs_.store(int64_t(session.inputLatency * 1e9),
                   std::memory_order_relaxed);
  }

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
      // buffer, which starts two periods ahead: the block waited there as
      // long as the frames still queued behind it.
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
  IosBackend* backend_;
  AudIoStream* stream_;
  DeviceRequest request_;
  DeviceFormat format_;
  ma_device device_{};
  bool initialized_ = false;
  std::atomic<int64_t> outputNs_{0};
  std::atomic<int64_t> inputNs_{0};
};

void dataCallback(ma_device* device, void* output, const void* input,
                  ma_uint32 frames) AUD_NONBLOCKING {
  static_cast<IosDevice*>(device->pUserData)->process(output, input, frames);
}

class IosBackend : public Backend {
 public:
  IosBackend(AudIoSession* session, const AudIoSessionConfig& config)
      : session_(session),
        config_(config),
        guard_(std::make_shared<Guard>()) {
    guard_->backend = this;
  }

  ~IosBackend() override {
    {
      std::lock_guard<std::mutex> lock(guard_->mutex);
      guard_->backend = nullptr;
    }
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    for (id observer : observers_) [center removeObserver:observer];
    observers_.clear();
    if (contextReady_) ma_context_uninit(&context_);
  }

  // The session that hears the permission's answer.
  AudIoSession* session() const { return session_; }

  // [control] Configures the session and starts to observe it.
  bool init() {
    ma_context_config contextConfig = ma_context_config_init();
    // The session belongs to this backend, not to miniaudio.
    contextConfig.coreaudio.sessionCategory = ma_ios_session_category_none;
    contextConfig.coreaudio.noAudioSessionActivate = MA_TRUE;
    contextConfig.coreaudio.noAudioSessionDeactivate = MA_TRUE;
    const ma_backend backends[] = {ma_backend_coreaudio};
    if (ma_context_init(backends, 1, &contextConfig, &context_) !=
        MA_SUCCESS) {
      return false;
    }
    contextReady_ = true;
    configure((config_.directions & AUD_IO_INPUT) != 0);
    observe();
    return true;
  }

  const char* name() const override { return "miniaudio/Core Audio"; }

  int32_t devices(std::vector<AudIoDevice>& out) override {
    @autoreleasepool {
      AVAudioSession* session = [AVAudioSession sharedInstance];
      const double rate = session.sampleRate;
      AVAudioSessionRouteDescription* route = session.currentRoute;
      bool first = true;
      for (AVAudioSessionPortDescription* port in route.outputs) {
        out.push_back(deviceOf(
            port, AUD_IO_OUTPUT,
            AUD_IO_DEVICE_ACTIVE | (first ? AUD_IO_DEVICE_DEFAULT_OUTPUT : 0),
            rate));
        first = false;
      }
      const std::string input = currentPort(false);
      for (AVAudioSessionPortDescription* port in session.availableInputs) {
        const bool active = stringOf(port.UID) == input;
        out.push_back(deviceOf(
            port, AUD_IO_INPUT,
            active ? AUD_IO_DEVICE_ACTIVE | AUD_IO_DEVICE_DEFAULT_INPUT : 0,
            rate));
      }
    }
    return AUD_OK;
  }

  int32_t permission() override {
    if (@available(iOS 17.0, *)) {
      switch ([AVAudioApplication sharedInstance].recordPermission) {
        case AVAudioApplicationRecordPermissionGranted:
          return AUD_IO_PERMISSION_GRANTED;
        case AVAudioApplicationRecordPermissionDenied:
          return AUD_IO_PERMISSION_DENIED;
        default:
          return AUD_IO_PERMISSION_UNDETERMINED;
      }
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    switch ([AVAudioSession sharedInstance].recordPermission) {
      case AVAudioSessionRecordPermissionGranted:
        return AUD_IO_PERMISSION_GRANTED;
      case AVAudioSessionRecordPermissionDenied:
        return AUD_IO_PERMISSION_DENIED;
      default:
        return AUD_IO_PERMISSION_UNDETERMINED;
    }
#pragma clang diagnostic pop
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
    if (@available(iOS 17.0, *)) {
      [AVAudioApplication requestRecordPermissionWithCompletionHandler:answer];
      return AUD_OK;
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [[AVAudioSession sharedInstance] requestRecordPermission:answer];
#pragma clang diagnostic pop
    return AUD_OK;
  }

  int32_t activate() override {
    NSError* error = nil;
    if ([[AVAudioSession sharedInstance] setActive:YES error:&error]) {
      return AUD_OK;
    }
    return error.code == AVAudioSessionErrorCodeCannotInterruptOthers ||
                   error.code == AVAudioSessionErrorCodeInsufficientPriority ||
                   error.code == AVAudioSessionErrorCodeIsBusy
               ? AUD_IO_ERROR_INTERRUPTED
               : AUD_IO_ERROR_DEVICE;
  }

  std::unique_ptr<Device> open(const DeviceRequest& request,
                               AudIoStream* stream,
                               int32_t* result) override {
    const bool input = (request.direction & AUD_IO_INPUT) != 0;
    if (input && permission() != AUD_IO_PERMISSION_GRANTED) {
      *result = AUD_IO_ERROR_PERMISSION;
      return nullptr;
    }
    // iOS routes the output itself: only the current output can be asked
    // for.
    if ((request.direction & AUD_IO_OUTPUT) != 0 &&
        !request.outputId.empty() &&
        request.outputId != currentPort(true)) {
      *result = AUD_IO_ERROR_NO_DEVICE;
      return nullptr;
    }
    if (input) {
      configure(true);
      if (!request.inputId.empty() && !preferInput(request.inputId)) {
        *result = AUD_IO_ERROR_NO_DEVICE;
        return nullptr;
      }
    }
    // The hardware's rate and channels are known once the session is
    // active; a stream that starts later activates it again.
    activate();
    auto device = std::make_unique<IosDevice>(this, stream, request);
    *result = device->open(&context_);
    if (*result != AUD_OK) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.push_back(device.get());
    return device;
  }

  // [control] A device closes.
  void remove(IosDevice* device) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.erase(std::remove(devices_.begin(), devices_.end(), device),
                   devices_.end());
  }

 private:
  // [control] Sets the category, its options and the mode; an input asks
  // for play-and-record.
  void configure(bool input) {
    @autoreleasepool {
      if (input) usesInput_ = true;
      AVAudioSession* session = [AVAudioSession sharedInstance];
      AVAudioSessionCategoryOptions options = 0;
      if ((config_.flags & AUD_IO_SESSION_MIX_WITH_OTHERS) != 0) {
        options |= AVAudioSessionCategoryOptionMixWithOthers;
      }
      AVAudioSessionCategory category = AVAudioSessionCategoryPlayback;
      if (usesInput_) {
        category = AVAudioSessionCategoryPlayAndRecord;
        options |= AVAudioSessionCategoryOptionDefaultToSpeaker |
                   AVAudioSessionCategoryOptionAllowBluetoothA2DP;
        if ((config_.flags & AUD_IO_SESSION_BLUETOOTH_HFP) != 0) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
          options |= AVAudioSessionCategoryOptionAllowBluetooth;
#pragma clang diagnostic pop
        }
      }
      AVAudioSessionMode mode = (config_.flags & AUD_IO_SESSION_MEASUREMENT) != 0
                                    ? AVAudioSessionModeMeasurement
                                    : AVAudioSessionModeDefault;
      if (![session.category isEqualToString:category] ||
          session.categoryOptions != options ||
          ![session.mode isEqualToString:mode]) {
        [session setCategory:category mode:mode options:options error:nil];
      }
    }
  }

  // [control] Prefers the available input with `uid`.
  bool preferInput(const std::string& uid) {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    for (AVAudioSessionPortDescription* port in session.availableInputs) {
      if (stringOf(port.UID) == uid) {
        return [session setPreferredInput:port error:nil];
      }
    }
    return false;
  }

  // Observes the session and the app; the blocks run on the threads the
  // notifications are posted on.
  void observe() {
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    AVAudioSession* session = [AVAudioSession sharedInstance];
    std::shared_ptr<Guard> guard = guard_;
    observers_.push_back([center
        addObserverForName:AVAudioSessionRouteChangeNotification
                    object:session
                     queue:nil
                usingBlock:^(NSNotification* note) {
                  std::lock_guard<std::mutex> lock(guard->mutex);
                  if (guard->backend == nullptr) return;
                  guard->backend->routeChanged(
                      [note.userInfo[AVAudioSessionRouteChangeReasonKey]
                          unsignedIntegerValue]);
                }]);
    observers_.push_back([center
        addObserverForName:AVAudioSessionInterruptionNotification
                    object:session
                     queue:nil
                usingBlock:^(NSNotification* note) {
                  std::lock_guard<std::mutex> lock(guard->mutex);
                  if (guard->backend == nullptr) return;
                  guard->backend->interrupted(note.userInfo);
                }]);
    observers_.push_back([center
        addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                    object:session
                     queue:nil
                usingBlock:^(NSNotification* note) {
                  std::lock_guard<std::mutex> lock(guard->mutex);
                  if (guard->backend == nullptr) return;
                  guard->backend->mediaServicesReset();
                }]);
    observers_.push_back([center
        addObserverForName:UIApplicationDidBecomeActiveNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* note) {
                  std::lock_guard<std::mutex> lock(guard->mutex);
                  if (guard->backend == nullptr) return;
                  guard->backend->becameActive();
                }]);
  }

  void routeChanged(NSUInteger reason) {
    const uint32_t why =
        reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable
            ? AUD_IO_REASON_DEVICE_REMOVED
        : reason == AVAudioSessionRouteChangeReasonNewDeviceAvailable
            ? AUD_IO_REASON_DEVICE_ADDED
            : AUD_IO_REASON_ROUTE_OVERRIDE;
    AVAudioSession* session = [AVAudioSession sharedInstance];
    const double rate = session.sampleRate;
    const uint32_t outputs = uint32_t(session.outputNumberOfChannels);
    const uint32_t inputs = uint32_t(session.inputNumberOfChannels);
    const std::string output = currentPort(true);
    const std::string input = currentPort(false);
    {
      // Under the lock: a stream lives at least as long as its device is
      // registered, and the stream functions take no lock of the stream's
      // control.
      std::lock_guard<std::mutex> lock(mutex_);
      for (IosDevice* device : devices_) {
        device->readLatencies();
        const DeviceFormat& format = device->format();
        const DeviceRequest& request = device->request();
        // A device that follows the hardware reopens for a new rate or a
        // new channel count; a gone input reopens on the new one.
        bool lost = request.sampleRate == 0 && rate > 0 &&
                    std::fabs(rate - format.sampleRate) > 0.5;
        if (format.outputChannels > 0 && request.outputChannels == 0 &&
            std::min(outputs, kDefaultChannels) != format.outputChannels) {
          lost = true;
        }
        if (format.inputChannels > 0 && request.inputChannels == 0 &&
            std::min(inputs, kDefaultChannels) != format.inputChannels) {
          lost = true;
        }
        if (format.inputChannels > 0 && format.inputId != input) lost = true;
        if (format.outputChannels > 0 && format.outputId != output &&
            why == AUD_IO_REASON_DEVICE_REMOVED) {
          lost = true;
        }
        if (lost) {
          streamDeviceLost(device->stream(), why);
        } else {
          streamRouteChanged(device->stream(), why);
        }
      }
    }
    sessionDevicesChanged(session_);
  }

  void interrupted(NSDictionary* info) {
    const NSUInteger type =
        [info[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionInterruptionTypeBegan) {
      uint32_t reason = AUD_IO_REASON_SYSTEM;
      if (@available(iOS 14.5, *)) {
        const NSUInteger why =
            [info[AVAudioSessionInterruptionReasonKey] unsignedIntegerValue];
        if (why == AVAudioSessionInterruptionReasonAppWasSuspended) {
          reason = AUD_IO_REASON_APP_SUSPENDED;
        } else if (why == AVAudioSessionInterruptionReasonBuiltInMicMuted) {
          reason = AUD_IO_REASON_BUILTIN_MIC_MUTED;
        }
      }
      interrupted_.store(true, std::memory_order_release);
      sessionInterruption(session_, true, reason);
    } else {
      interrupted_.store(false, std::memory_order_release);
      sessionInterruption(session_, false, AUD_IO_REASON_NONE);
    }
  }

  void mediaServicesReset() {
    // Every audio object of the app is gone: configure the session again
    // and open every device anew.
    configure(usesInput_);
    sessionAllDevicesLost(session_, AUD_IO_REASON_MEDIA_SERVICES_RESET);
  }

  void becameActive() {
    // iOS does not always end an interruption; the return to the
    // foreground does.
    if (interrupted_.exchange(false, std::memory_order_acq_rel)) {
      sessionInterruption(session_, false, AUD_IO_REASON_NONE);
    }
  }

  AudIoSession* session_;
  AudIoSessionConfig config_;
  ma_context context_{};
  bool contextReady_ = false;
  bool usesInput_ = false;
  std::atomic<bool> interrupted_{false};
  std::shared_ptr<Guard> guard_;
  std::vector<id> observers_;
  std::mutex mutex_;
  std::vector<IosDevice*> devices_;
};

IosDevice::~IosDevice() {
  backend_->remove(this);
  if (initialized_) ma_device_uninit(&device_);
}

int32_t IosDevice::open(ma_context* context) {
  AVAudioSession* session = [AVAudioSession sharedInstance];
  const bool output = (request_.direction & AUD_IO_OUTPUT) != 0;
  const bool input = (request_.direction & AUD_IO_INPUT) != 0;
  ma_device_config config = ma_device_config_init(
      output && input ? ma_device_type_duplex
      : output        ? ma_device_type_playback
                      : ma_device_type_capture);
  config.playback.format = ma_format_f32;
  config.playback.channels =
      request_.outputChannels > 0
          ? request_.outputChannels
          : std::min(uint32_t(session.outputNumberOfChannels),
                     kDefaultChannels);
  config.capture.format = ma_format_f32;
  config.capture.channels =
      request_.inputChannels > 0
          ? request_.inputChannels
          : std::max(1u, std::min(uint32_t(session.inputNumberOfChannels),
                                  kDefaultChannels));
  // miniaudio asks the session for the rate it is given, 0 included; the
  // device's rate is the session's current one.
  const double hardware = session.sampleRate > 0 ? session.sampleRate
                                                 : kFallbackRate;
  const double rate =
      request_.sampleRate > 0 ? request_.sampleRate : hardware;
  config.sampleRate = ma_uint32(std::lround(rate));
  config.periodSizeInFrames =
      request_.bufferFrames > 0
          ? request_.bufferFrames
          : ma_uint32(std::lround(rate * kDefaultBufferSeconds));
  config.performanceProfile = request_.performanceMode ==
                                      AUD_IO_PERFORMANCE_POWER_SAVING
                                  ? ma_performance_profile_conservative
                                  : ma_performance_profile_low_latency;
  // Callbacks of the size the hardware asks for; the stream splits them.
  config.noFixedSizedCallback = MA_TRUE;
  config.dataCallback = dataCallback;
  config.pUserData = this;
  if (ma_device_init(context, &config, &device_) != MA_SUCCESS) {
    return AUD_IO_ERROR_DEVICE;
  }
  initialized_ = true;
  format_.sampleRate = device_.sampleRate;
  format_.outputChannels = output ? device_.playback.channels : 0;
  format_.inputChannels = input ? device_.capture.channels : 0;
  // What the session granted, not what miniaudio asked for.
  format_.bufferFrames = uint32_t(
      std::lround(session.IOBufferDuration * double(device_.sampleRate)));
  format_.burstFrames = output ? device_.playback.internalPeriodSizeInFrames
                               : device_.capture.internalPeriodSizeInFrames;
  format_.performanceMode = request_.performanceMode;
  format_.exclusive = false;
  format_.timeSource = AUD_TIME_SOURCE_ESTIMATED;
  if (output) format_.outputId = currentPort(true);
  if (input) format_.inputId = currentPort(false);
  format_.backend = "miniaudio/Core Audio";
  readLatencies();
  return AUD_OK;
}

}  // namespace

std::unique_ptr<Backend> createPlatformBackend(
    AudIoSession* session, const AudIoSessionConfig& config) {
  auto backend = std::make_unique<IosBackend>(session, config);
  if (!backend->init()) return nullptr;
  return backend;
}

}  // namespace aud_io

#endif  // TARGET_OS_IPHONE
