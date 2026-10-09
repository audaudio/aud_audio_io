// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The null backend: devices that keep time without hardware, for the tests
// and for the platforms whose devices arrive with a later ticket (S3b to
// S3d). A device calls back on a thread of its own, paced by the host clock,
// or - with the manual clock - whenever the test calls aud_io_null_process.
// Tests inject the faults of real devices: disconnects, changes of rate and
// channels, interruptions, late callbacks, xruns, hot-plugs, failing opens
// and the microphone permission. A duplex device loops its output back into
// its input after exactly the output plus the input latency it reports, so
// that the latency probe measures the round trip the stream reports.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aud_clock.h"
#include "aud_io_internal.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace aud_io {
namespace {

constexpr double kDefaultRate = 48000;
constexpr uint32_t kDefaultBufferFrames = 256;
constexpr uint32_t kMaxCallbackFrames = 4096;
constexpr uint32_t kDefaultDeviceChannels = 2;
constexpr uint32_t kRouteLatencyStep = 64;
constexpr const char* kOutputId = "null:out";
constexpr const char* kInputId = "null:in";

class NullBackend;

// A plugged device of the null backend.
struct Plugged {
  std::string id;
  uint32_t channels = 0;
};

class NullDevice : public Device {
 public:
  NullDevice(NullBackend* backend, AudIoStream* stream, DeviceFormat format,
             uint32_t direction, std::vector<uint32_t> blockSizes,
             bool manual);

  // [control] Whether the next start fails; the backend counts them down.
  std::function<bool()> failStart;
  ~NullDevice() override;

  int32_t start() override;
  int32_t stop() override;
  const DeviceFormat& format() const override { return format_; }
  uint64_t xruns() const override {
    return xruns_.load(std::memory_order_relaxed);
  }

  AudIoStream* stream() const { return stream_; }

  // [control] The faults.
  void lose() { gone_.store(true, std::memory_order_release); }
  void delay(int64_t ms) { lateMs_.store(ms, std::memory_order_release); }
  void addXruns(uint64_t count) {
    xruns_.fetch_add(count, std::memory_order_relaxed);
  }
  void addRouteLatency(uint32_t frames) {
    routeLatency_.fetch_add(frames, std::memory_order_relaxed);
  }

  // The manual clock: one callback; the caller holds `processMutex`.
  int32_t manual(const float* input, float* output, uint32_t frames,
                 int64_t hostTimeNs);

  std::mutex processMutex;

 private:
  void run();
  void callback(const float* input, float* output, uint32_t frames,
                int64_t scheduledNs, int64_t callbackNs);
  uint32_t nextBlockSize();

  NullBackend* backend_;
  AudIoStream* stream_;
  DeviceFormat format_;
  uint32_t direction_;
  std::vector<uint32_t> blockSizes_;
  size_t blockIndex_ = 0;
  bool manual_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopThread_{false};
  std::atomic<bool> gone_{false};
  std::atomic<int64_t> lateMs_{0};
  std::atomic<uint64_t> xruns_{0};
  std::atomic<uint32_t> routeLatency_{0};
  std::thread thread_;
  // The interleaved buffers of a callback and the history of the output the
  // loop feeds back into the input.
  std::vector<float> output_;
  std::vector<float> input_;
  std::vector<float> loop_;
  uint32_t loopChannels_ = 0;
  int64_t devicePosition_ = 0;
};

class NullBackend : public Backend {
 public:
  NullBackend(AudIoSession* session, const AudIoSessionConfig& config)
      : session_(session),
        manual_((config.flags & AUD_IO_SESSION_MANUAL_CLOCK) != 0) {}

  const char* name() const override { return "null"; }

  int32_t devices(std::vector<AudIoDevice>& out) override {
    std::lock_guard<std::mutex> lock(mutex_);
    out.push_back(device(kOutputId, "Null output", AUD_IO_OUTPUT,
                         AUD_IO_DEVICE_DEFAULT_OUTPUT, AUD_IO_ROUTE_VIRTUAL,
                         outputChannels_));
    out.push_back(device(kInputId, "Null input", AUD_IO_INPUT,
                         AUD_IO_DEVICE_DEFAULT_INPUT, AUD_IO_ROUTE_VIRTUAL,
                         kDefaultDeviceChannels));
    for (const Plugged& plugged : plugged_) {
      out.push_back(device(plugged.id, "Null interface", AUD_IO_DUPLEX, 0,
                           AUD_IO_ROUTE_USB, plugged.channels));
    }
    return AUD_OK;
  }

  int32_t permission() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return permission_;
  }

  int32_t requestPermission() override {
    int32_t answer;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (permission_ == AUD_IO_PERMISSION_UNDETERMINED) {
        permission_ = AUD_IO_PERMISSION_GRANTED;
      }
      answer = permission_;
    }
    sessionPermissionChanged(session_, answer);
    return AUD_OK;
  }

  int32_t activate() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return interrupted_ ? AUD_IO_ERROR_INTERRUPTED : AUD_OK;
  }

  std::unique_ptr<Device> open(const DeviceRequest& request,
                               AudIoStream* stream,
                               int32_t* result) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failOpens_ > 0) {
      failOpens_ -= 1;
      *result = AUD_IO_ERROR_DEVICE;
      return nullptr;
    }
    if (aud_clock_now_ns() < outageUntilNs_) {
      *result = AUD_IO_ERROR_NO_DEVICE;
      return nullptr;
    }
    const bool output = (request.direction & AUD_IO_OUTPUT) != 0;
    const bool input = (request.direction & AUD_IO_INPUT) != 0;
    if (input && permission_ != AUD_IO_PERMISSION_GRANTED) {
      *result = AUD_IO_ERROR_PERMISSION;
      return nullptr;
    }
    DeviceFormat format;
    uint32_t outputMax = 0;
    uint32_t inputMax = 0;
    if (output && !resolve(request.outputId, true, &format.outputId,
                           &outputMax)) {
      *result = AUD_IO_ERROR_NO_DEVICE;
      return nullptr;
    }
    if (input &&
        !resolve(request.inputId, false, &format.inputId, &inputMax)) {
      *result = AUD_IO_ERROR_NO_DEVICE;
      return nullptr;
    }
    format.sampleRate = request.sampleRate > 0 ? request.sampleRate : rate_;
    format.outputChannels =
        !output ? 0
        : request.outputChannels > 0
            ? request.outputChannels
            : std::min(outputMax, kDefaultChannels);
    format.inputChannels =
        !input ? 0
        : request.inputChannels > 0 ? request.inputChannels
                                    : std::min(inputMax, kDefaultChannels);
    format.bufferFrames =
        request.bufferFrames > 0 ? request.bufferFrames : kDefaultBufferFrames;
    format.burstFrames = format.bufferFrames;
    format.performanceMode = request.performanceMode;
    format.exclusive = (request.flags & AUD_IO_STREAM_EXCLUSIVE) != 0;
    format.timeSource = AUD_TIME_SOURCE_HARDWARE;
    format.backend = "null";
    auto device = std::make_unique<NullDevice>(
        this, stream, format, request.direction, blockSizes_, manual_);
    device->failStart = [this] { return takeFailedStart(); };
    devices_.push_back(device.get());
    *result = AUD_OK;
    return device;
  }

  int32_t inject(uint32_t fault, int64_t value) override {
    uint32_t reason = AUD_IO_REASON_DEVICE_REMOVED;
    bool devicesChanged = false;
    bool permissionChanged = false;
    int32_t permission = AUD_IO_PERMISSION_UNDETERMINED;
    {
      // The streams are told under the lock: a stream lives at least as long
      // as its device is registered here, and the stream functions take no
      // lock of the stream's control.
      std::lock_guard<std::mutex> lock(mutex_);
      std::vector<AudIoStream*> lost;
      std::vector<AudIoStream*> routed;
      switch (fault) {
        case AUD_IO_FAULT_DISCONNECT:
          if (value < 0) return AUD_ERROR_INVALID_ARGUMENT;
          outageUntilNs_ = aud_clock_now_ns() + value * 1'000'000;
          loseAll(lost);
          break;
        case AUD_IO_FAULT_SAMPLE_RATE:
          if (value < 8000 || value > 384000) {
            return AUD_ERROR_INVALID_ARGUMENT;
          }
          rate_ = static_cast<double>(value);
          reason = AUD_IO_REASON_SAMPLE_RATE;
          loseAll(lost);
          break;
        case AUD_IO_FAULT_INTERRUPT:
          interrupted_ = value != 0;
          break;
        case AUD_IO_FAULT_LATE:
          if (value <= 0) return AUD_ERROR_INVALID_ARGUMENT;
          for (NullDevice* device : devices_) device->delay(value);
          break;
        case AUD_IO_FAULT_XRUN:
          if (value <= 0) return AUD_ERROR_INVALID_ARGUMENT;
          for (NullDevice* device : devices_) {
            device->addXruns(static_cast<uint64_t>(value));
          }
          break;
        case AUD_IO_FAULT_HOT_PLUG:
          if (value < 0 || value > AUD_IO_MAX_CHANNELS) {
            return AUD_ERROR_INVALID_ARGUMENT;
          }
          if (value > 0) {
            plugged_.push_back({"null:usb:" + std::to_string(++plugCount_),
                                static_cast<uint32_t>(value)});
          } else {
            if (plugged_.empty()) return AUD_ERROR_STATE;
            const std::string id = plugged_.back().id;
            plugged_.pop_back();
            for (NullDevice* device : devices_) {
              if (device->format().outputId == id ||
                  device->format().inputId == id) {
                device->lose();
                lost.push_back(device->stream());
              }
            }
          }
          devicesChanged = true;
          break;
        case AUD_IO_FAULT_FAIL_OPEN:
          if (value < 0) return AUD_ERROR_INVALID_ARGUMENT;
          failOpens_ = value;
          break;
        case AUD_IO_FAULT_FAIL_START:
          if (value < 0) return AUD_ERROR_INVALID_ARGUMENT;
          failStarts_.store(value, std::memory_order_relaxed);
          break;
        case AUD_IO_FAULT_PERMISSION:
          if (value < AUD_IO_PERMISSION_UNDETERMINED ||
              value > AUD_IO_PERMISSION_GRANTED) {
            return AUD_ERROR_INVALID_ARGUMENT;
          }
          permission_ = static_cast<int32_t>(value);
          permission = permission_;
          permissionChanged = true;
          break;
        case AUD_IO_FAULT_ROUTE:
          for (NullDevice* device : devices_) {
            device->addRouteLatency(kRouteLatencyStep);
            routed.push_back(device->stream());
          }
          break;
        case AUD_IO_FAULT_CHANNELS:
          if (value <= 0 || value > AUD_IO_MAX_CHANNELS) {
            return AUD_ERROR_INVALID_ARGUMENT;
          }
          outputChannels_ = static_cast<uint32_t>(value);
          reason = AUD_IO_REASON_ROUTE_OVERRIDE;
          loseAll(lost);
          break;
        default:
          return AUD_ERROR_INVALID_ARGUMENT;
      }
      for (AudIoStream* stream : lost) streamDeviceLost(stream, reason);
      for (AudIoStream* stream : routed) {
        streamRouteChanged(stream, AUD_IO_REASON_ROUTE_OVERRIDE);
      }
    }
    if (fault == AUD_IO_FAULT_INTERRUPT) {
      sessionInterruption(session_, value != 0, AUD_IO_REASON_SYSTEM);
    }
    if (devicesChanged) sessionDevicesChanged(session_);
    if (permissionChanged) sessionPermissionChanged(session_, permission);
    return AUD_OK;
  }

  int32_t setBlockSizes(const uint32_t* sizes, uint32_t count) override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t i = 0; i < count; ++i) {
      if (sizes[i] == 0 || sizes[i] > kMaxCallbackFrames) {
        return AUD_ERROR_INVALID_ARGUMENT;
      }
    }
    blockSizes_.assign(sizes, sizes + count);
    return AUD_OK;
  }

  int32_t process(AudIoStream* stream, const float* input, float* output,
                  uint32_t frames, int64_t hostTimeNs) override {
    if (!manual_) return AUD_ERROR_STATE;
    if (frames > kMaxCallbackFrames) return AUD_ERROR_INVALID_ARGUMENT;
    std::unique_lock<std::mutex> lock(mutex_);
    NullDevice* device = nullptr;
    for (NullDevice* candidate : devices_) {
      if (candidate->stream() == stream) device = candidate;
    }
    if (device == nullptr) return AUD_ERROR_STATE;
    std::lock_guard<std::mutex> processing(device->processMutex);
    lock.unlock();
    return device->manual(input, output, frames, hostTimeNs);
  }

  // [control] Counts a failing start down; true while starts fail.
  bool takeFailedStart() {
    int64_t left = failStarts_.load(std::memory_order_relaxed);
    while (left > 0) {
      if (failStarts_.compare_exchange_weak(left, left - 1)) return true;
    }
    return false;
  }

  // [control] A device closes.
  void remove(NullDevice* device) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.erase(std::remove(devices_.begin(), devices_.end(), device),
                   devices_.end());
  }

 private:
  static AudIoDevice device(const std::string& id, const char* name,
                            uint32_t directions, uint32_t flags,
                            uint32_t route, uint32_t channels) {
    AudIoDevice device{};
    device.struct_size = sizeof(AudIoDevice);
    device.directions = directions;
    device.route = route;
    device.flags = flags;
    device.max_output_channels =
        (directions & AUD_IO_OUTPUT) != 0 ? channels : 0;
    device.max_input_channels = (directions & AUD_IO_INPUT) != 0 ? channels : 0;
    device.num_sample_rates = 0;
    copyString(device.id, sizeof(device.id), id);
    copyString(device.name, sizeof(device.name), name);
    return device;
  }

  // Resolves a requested device id; the lock is held.
  bool resolve(const std::string& requested, bool output, std::string* id,
               uint32_t* channels) const {
    const char* fallback = output ? kOutputId : kInputId;
    if (requested.empty() || requested == fallback) {
      *id = fallback;
      *channels = output ? outputChannels_ : kDefaultDeviceChannels;
      return true;
    }
    for (const Plugged& plugged : plugged_) {
      if (plugged.id == requested) {
        *id = plugged.id;
        *channels = plugged.channels;
        return true;
      }
    }
    return false;
  }

  // Marks every open device as gone; the lock is held.
  void loseAll(std::vector<AudIoStream*>& lost) {
    for (NullDevice* device : devices_) {
      device->lose();
      lost.push_back(device->stream());
    }
  }

  AudIoSession* session_;
  const bool manual_;
  std::mutex mutex_;
  std::vector<NullDevice*> devices_;
  std::vector<Plugged> plugged_;
  std::vector<uint32_t> blockSizes_;
  double rate_ = kDefaultRate;
  uint32_t outputChannels_ = kDefaultDeviceChannels;
  int32_t permission_ = AUD_IO_PERMISSION_GRANTED;
  int64_t failOpens_ = 0;
  std::atomic<int64_t> failStarts_{0};
  int64_t outageUntilNs_ = 0;
  int64_t plugCount_ = 0;
  bool interrupted_ = false;
};

// ............................................................................
// NullDevice

NullDevice::NullDevice(NullBackend* backend, AudIoStream* stream,
                       DeviceFormat format, uint32_t direction,
                       std::vector<uint32_t> blockSizes, bool manual)
    : backend_(backend),
      stream_(stream),
      format_(std::move(format)),
      direction_(direction),
      blockSizes_(std::move(blockSizes)),
      manual_(manual) {
  output_.assign(size_t{kMaxCallbackFrames} * format_.outputChannels, 0.0f);
  input_.assign(size_t{kMaxCallbackFrames} * format_.inputChannels, 0.0f);
  if (direction_ == AUD_IO_DUPLEX) {
    loopChannels_ = std::min(format_.outputChannels, format_.inputChannels);
    // The loop holds the output until it returns at the input.
    const size_t history = size_t{2} * format_.bufferFrames +
                           kRouteLatencyStep * 64 + kMaxCallbackFrames;
    loop_.assign(history * loopChannels_, 0.0f);
  }
}

NullDevice::~NullDevice() {
  backend_->remove(this);
  stop();
  std::lock_guard<std::mutex> processing(processMutex);
}

int32_t NullDevice::start() {
  if (running_.load(std::memory_order_acquire)) return AUD_ERROR_STATE;
  if (gone_.load(std::memory_order_acquire)) return AUD_IO_ERROR_NO_DEVICE;
  if (failStart && failStart()) return AUD_IO_ERROR_DEVICE;
  if (manual_) {
    std::lock_guard<std::mutex> processing(processMutex);
    running_.store(true, std::memory_order_release);
    return AUD_OK;
  }
  stopThread_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  thread_ = std::thread([this] { run(); });
  return AUD_OK;
}

int32_t NullDevice::stop() {
  if (manual_) {
    std::lock_guard<std::mutex> processing(processMutex);
    running_.store(false, std::memory_order_release);
    return AUD_OK;
  }
  stopThread_.store(true, std::memory_order_release);
  if (thread_.joinable()) thread_.join();
  running_.store(false, std::memory_order_release);
  return AUD_OK;
}

uint32_t NullDevice::nextBlockSize() {
  if (blockSizes_.empty()) {
    return std::min(format_.bufferFrames, kMaxCallbackFrames);
  }
  const uint32_t size = blockSizes_[blockIndex_ % blockSizes_.size()];
  blockIndex_ += 1;
  return size;
}

void NullDevice::callback(const float* input, float* output, uint32_t frames,
                          int64_t scheduledNs, int64_t callbackNs) {
  const double nsPerFrame = 1e9 / format_.sampleRate;
  const uint32_t outputLatency =
      format_.bufferFrames + routeLatency_.load(std::memory_order_relaxed);
  const uint32_t inputLatency = format_.bufferFrames;
  DeviceTime time;
  time.callbackNs = callbackNs;
  time.source = AUD_TIME_SOURCE_HARDWARE;
  if (format_.outputChannels > 0) {
    time.outputLatencyFrames = outputLatency;
    time.outputNs =
        scheduledNs + static_cast<int64_t>(std::llround(outputLatency * nsPerFrame));
  }
  if (format_.inputChannels > 0) {
    time.inputLatencyFrames = inputLatency;
    time.inputNs =
        scheduledNs - static_cast<int64_t>(std::llround(inputLatency * nsPerFrame));
  }
  // The loop: the input of this block is the output that left
  // `outputLatency + inputLatency` frames earlier.
  if (input == nullptr && format_.inputChannels > 0) {
    std::fill(input_.begin(), input_.begin() + size_t{frames} * format_.inputChannels,
              0.0f);
    if (loopChannels_ > 0) {
      const size_t history = loop_.size() / loopChannels_;
      const int64_t delay = int64_t{outputLatency} + inputLatency;
      for (uint32_t f = 0; f < frames; ++f) {
        // Output that has not left yet, or left too long ago, is silence.
        const int64_t source = devicePosition_ + f - delay;
        if (source < 0 || source >= devicePosition_ ||
            delay >= int64_t(history)) {
          continue;
        }
        const size_t slot = static_cast<size_t>(source % int64_t(history));
        for (uint32_t c = 0; c < loopChannels_; ++c) {
          input_[size_t{f} * format_.inputChannels + c] =
              loop_[slot * loopChannels_ + c];
        }
      }
    }
    input = input_.data();
  }
  float* target = output != nullptr ? output : output_.data();
  streamProcess(stream_, format_.inputChannels > 0 ? input : nullptr,
                format_.outputChannels > 0 ? target : nullptr, frames, time);
  if (loopChannels_ > 0) {
    const size_t history = loop_.size() / loopChannels_;
    for (uint32_t f = 0; f < frames; ++f) {
      const size_t slot =
          static_cast<size_t>((devicePosition_ + f) % int64_t(history));
      for (uint32_t c = 0; c < loopChannels_; ++c) {
        loop_[slot * loopChannels_ + c] =
            target[size_t{f} * format_.outputChannels + c];
      }
    }
  }
  devicePosition_ += frames;
}

int32_t NullDevice::manual(const float* input, float* output, uint32_t frames,
                           int64_t hostTimeNs) {
  if (!running_.load(std::memory_order_acquire) ||
      gone_.load(std::memory_order_acquire)) {
    return AUD_ERROR_STATE;
  }
  callback(input, output, frames, hostTimeNs, hostTimeNs);
  return AUD_OK;
}

void NullDevice::run() {
  const double nsPerFrame = 1e9 / format_.sampleRate;
  int64_t due = aud_clock_now_ns();
  while (!stopThread_.load(std::memory_order_acquire)) {
    const int64_t late = lateMs_.exchange(0, std::memory_order_acq_rel);
    if (late > 0) {
      // The callback misses its deadline: the device plays silence.
      xruns_.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::milliseconds(late));
    }
    const int64_t wait = due - aud_clock_now_ns();
    if (wait > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
    if (stopThread_.load(std::memory_order_acquire)) break;
    if (gone_.load(std::memory_order_acquire)) {
      // A device that went away calls back no more.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    const uint32_t frames = nextBlockSize();
    callback(nullptr, nullptr, frames, due, aud_clock_now_ns());
    due += static_cast<int64_t>(std::llround(frames * nsPerFrame));
  }
}

}  // namespace

std::unique_ptr<Backend> createNullBackend(AudIoSession* session,
                                           const AudIoSessionConfig& config) {
  return std::make_unique<NullBackend>(session, config);
}

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
// macOS, Windows and Linux get their devices with S3b to S3d.
std::unique_ptr<Backend> createPlatformBackend(
    AudIoSession* session, const AudIoSessionConfig& config) {
  return createNullBackend(session, config);
}
#endif

}  // namespace aud_io
