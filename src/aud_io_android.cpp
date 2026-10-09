// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The Android backend (io-002): Oboe with AAudio, and OpenSL ES below API
// 27. Output, input and full duplex through Oboe's FullDuplexStream; the
// latency tuner on the output; presentation and capture times from
// AAudio's timestamps (time-001); xruns from AAudio. A disconnect - AAudio
// closes the stream when the route changes - reaches the worker of the
// stream, which opens the device again (lifecycle-001). The devices, the
// audio focus and the microphone permission live in Java and come from the
// Dart side (ticket 21, decision 1).

#if defined(__ANDROID__)

#include <time.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aud_clock.h"
#include "aud_io_internal.hpp"
#include "oboe/Oboe.h"
#include "oboe/OboeExtensions.h"

namespace aud_io {
namespace {

class AndroidDevice;

oboe::PerformanceMode performanceModeOf(uint32_t mode) {
  switch (mode) {
    case AUD_IO_PERFORMANCE_NONE:
      return oboe::PerformanceMode::None;
    case AUD_IO_PERFORMANCE_POWER_SAVING:
      return oboe::PerformanceMode::PowerSaving;
    default:
      return oboe::PerformanceMode::LowLatency;
  }
}

uint32_t performanceModeOf(oboe::PerformanceMode mode) {
  switch (mode) {
    case oboe::PerformanceMode::None:
      return AUD_IO_PERFORMANCE_NONE;
    case oboe::PerformanceMode::PowerSaving:
      return AUD_IO_PERFORMANCE_POWER_SAVING;
    default:
      return AUD_IO_PERFORMANCE_LOW_LATENCY;
  }
}

// The callbacks Oboe holds by shared pointer: Oboe's error thread keeps the
// stream and with it these callbacks alive while it runs, even when the
// device closes meanwhile.
class Callbacks : public oboe::AudioStreamDataCallback,
                  public oboe::AudioStreamErrorCallback {
 public:
  explicit Callbacks(AndroidDevice* device, AudIoStream* stream)
      : device_(device), stream_(stream) {}

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* oboeStream,
                                        void* audioData,
                                        int32_t numFrames) override;

  void onErrorAfterClose(oboe::AudioStream* oboeStream,
                         oboe::Result error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_ == nullptr) return;
    streamDeviceLost(stream_, error == oboe::Result::ErrorDisconnected
                                  ? AUD_IO_REASON_DEVICE_REMOVED
                                  : AUD_IO_REASON_ERROR);
  }

  // [control] The device closes: no data callback runs any more, and an
  // error that arrives late finds no stream.
  void detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    device_.store(nullptr, std::memory_order_release);
    stream_ = nullptr;
  }

 private:
  std::atomic<AndroidDevice*> device_;
  std::mutex mutex_;
  AudIoStream* stream_;
};

// Full duplex: the output stream's callback reads the input stream. The
// input goes into the device's buffer, sized by the input channels:
// FullDuplexStream sizes its own by the output channels, which a stream
// with more inputs than outputs would overrun.
class Duplex : public oboe::FullDuplexStream {
 public:
  explicit Duplex(AndroidDevice* device) : device_(device) {}

  oboe::ResultWithValue<int32_t> readInput(int32_t numFrames) override;

  oboe::DataCallbackResult onBothStreamsReady(const void* inputData,
                                              int numInputFrames,
                                              void* outputData,
                                              int numOutputFrames) override;

  void detach() { device_.store(nullptr, std::memory_order_release); }

 private:
  std::atomic<AndroidDevice*> device_;
};

class AndroidDevice : public Device {
 public:
  AndroidDevice(AudIoStream* stream, const DeviceRequest& request)
      : stream_(stream), request_(request) {
    callbacks_ = std::make_shared<Callbacks>(this, stream);
    if (request.direction == AUD_IO_DUPLEX) {
      duplex_ = std::make_shared<Duplex>(this);
    }
  }

  ~AndroidDevice() override {
    stop();
    callbacks_->detach();
    if (output_ != nullptr) output_->close();
    if (input_ != nullptr) input_->close();
    if (duplex_ != nullptr) {
      duplex_->detach();
      // The output stream keeps the helper as its data callback after the
      // close, and the helper keeps both streams: without this the four
      // would keep each other alive.
      std::shared_ptr<oboe::AudioStream> none;
      duplex_->setSharedInputStream(none);
      duplex_->setSharedOutputStream(none);
    }
  }

  // [control] Opens the Oboe streams; AUD_OK or the reason.
  int32_t open() {
    const bool output = (request_.direction & AUD_IO_OUTPUT) != 0;
    const bool input = (request_.direction & AUD_IO_INPUT) != 0;
    if (output) {
      const int32_t result =
          openStream(oboe::Direction::Output, request_.outputId,
                     request_.outputChannels, request_.sampleRate, &output_);
      if (result != AUD_OK) return result;
    }
    if (input) {
      // A duplex input runs at the rate of the output.
      const double rate =
          output_ != nullptr ? output_->getSampleRate() : request_.sampleRate;
      const int32_t result =
          openStream(oboe::Direction::Input, request_.inputId,
                     request_.inputChannels, rate, &input_);
      if (result != AUD_OK) return result;
    }
    if (output_ != nullptr) {
      const int32_t burst = output_->getFramesPerBurst();
      if (request_.bufferFrames > 0) {
        output_->setBufferSizeInFrames(
            static_cast<int32_t>(request_.bufferFrames));
      } else if ((request_.flags & AUD_IO_STREAM_LATENCY_TUNER) != 0) {
        tuner_ = std::make_unique<oboe::LatencyTuner>(*output_);
      } else if (burst > 0) {
        output_->setBufferSizeInFrames(burst * 2);
      }
    }
    if (duplex_ != nullptr) {
      duplex_->setSharedInputStream(input_);
      duplex_->setSharedOutputStream(output_);
      // The input arrives in blocks up to the output's capacity.
      const int32_t capacity = std::max(output_->getBufferCapacityInFrames(),
                                        output_->getFramesPerBurst() * 4);
      inputPad_.assign(
          size_t(capacity) * size_t(input_->getChannelCount()), 0.0f);
    }
    describe();
    return AUD_OK;
  }

  int32_t start() override {
    oboe::Result result;
    if (duplex_ != nullptr) {
      result = duplex_->start();
    } else if (output_ != nullptr) {
      result = output_->requestStart();
    } else {
      result = input_->requestStart();
    }
    if (result != oboe::Result::OK) return AUD_IO_ERROR_DEVICE;
    running_ = true;
    return AUD_OK;
  }

  int32_t stop() override {
    if (!running_) return AUD_OK;
    running_ = false;
    // stop() waits until the callback returned.
    if (duplex_ != nullptr) {
      duplex_->stop();
    } else if (output_ != nullptr) {
      output_->stop();
    } else if (input_ != nullptr) {
      input_->stop();
    }
    return AUD_OK;
  }

  const DeviceFormat& format() const override { return format_; }

  uint64_t xruns() const override {
    uint64_t count = 0;
    for (const auto& stream : {output_, input_}) {
      if (stream == nullptr) continue;
      const auto xruns = stream->getXRunCount();
      if (xruns && xruns.value() > 0) count += uint64_t(xruns.value());
    }
    return count;
  }

  // ..........................................................................
  // The audio thread

  void outputReady(float* output, int32_t frames) AUD_NONBLOCKING {
    if (tuner_ != nullptr) tuner_->tune();
    const DeviceTime time = timeOf(frames, 0);
    streamProcess(stream_, nullptr, output, uint32_t(frames), time);
  }

  void inputReady(const float* input, int32_t frames) AUD_NONBLOCKING {
    const DeviceTime time = timeOf(0, frames);
    streamProcess(stream_, input, nullptr, uint32_t(frames), time);
  }

  // Reads up to `frames` input frames into the device's buffer.
  oboe::ResultWithValue<int32_t> readInput(int32_t frames) AUD_NONBLOCKING {
    const size_t channels = size_t(format_.inputChannels);
    const size_t capacity = channels == 0 ? 0 : inputPad_.size() / channels;
    const int32_t wanted = std::min(frames, int32_t(capacity));
    return input_->read(inputPad_.data(), wanted, 0);
  }

  void duplexReady(int32_t inputFrames, float* output,
                   int32_t outputFrames) AUD_NONBLOCKING {
    if (tuner_ != nullptr) tuner_->tune();
    // The input may come short; the rest of the block is silence.
    const size_t channels = size_t(format_.inputChannels);
    const size_t capacity = channels == 0 ? 0 : inputPad_.size() / channels;
    const size_t frames = std::min(size_t(outputFrames), capacity);
    const size_t available =
        std::min(size_t(std::max(inputFrames, 0)), frames);
    if (frames > available) {
      std::memset(inputPad_.data() + available * channels, 0,
                  (frames - available) * channels * sizeof(float));
    }
    const DeviceTime time = timeOf(int32_t(frames), inputFrames);
    streamProcess(stream_, inputPad_.data(), output, uint32_t(frames), time);
  }

 private:
  int32_t openStream(oboe::Direction direction, const std::string& id,
                     uint32_t channels, double sampleRate,
                     std::shared_ptr<oboe::AudioStream>* out) {
    const bool output = direction == oboe::Direction::Output;
    int32_t channelCount = channels > 0 ? int32_t(channels)
                                        : int32_t(oboe::kUnspecified);
    for (int attempt = 0; attempt < 2; ++attempt) {
      oboe::AudioStreamBuilder builder;
      builder.setDirection(direction)
          ->setPerformanceMode(performanceModeOf(request_.performanceMode))
          ->setSharingMode((request_.flags & AUD_IO_STREAM_EXCLUSIVE) != 0
                               ? oboe::SharingMode::Exclusive
                               : oboe::SharingMode::Shared)
          ->setFormat(oboe::AudioFormat::Float)
          ->setFormatConversionAllowed(true)
          ->setChannelConversionAllowed(true)
          ->setChannelCount(channelCount)
          ->setErrorCallback(callbacks_);
      if (output) {
        builder.setUsage(oboe::Usage::Media)
            ->setContentType(oboe::ContentType::Music);
        // A duplex output calls the duplex helper, which reads the input.
        if (duplex_ != nullptr) {
          builder.setDataCallback(duplex_);
        } else {
          builder.setDataCallback(callbacks_);
        }
      } else {
        builder.setInputPreset(oboe::getSdkVersion() >= 29
                                   ? oboe::InputPreset::VoicePerformance
                                   : oboe::InputPreset::Unprocessed);
        if ((request_.direction & AUD_IO_OUTPUT) == 0) {
          builder.setDataCallback(callbacks_);
        }
      }
      if (sampleRate > 0) {
        builder.setSampleRate(int32_t(sampleRate))
            ->setSampleRateConversionQuality(
                oboe::SampleRateConversionQuality::Medium);
      }
      if (!id.empty()) {
        char* end = nullptr;
        const long deviceId = std::strtol(id.c_str(), &end, 10);
        if (end == id.c_str() || *end != '\0') return AUD_IO_ERROR_NO_DEVICE;
        builder.setDeviceId(int32_t(deviceId));
      }
      std::shared_ptr<oboe::AudioStream> stream;
      const oboe::Result result = builder.openStream(stream);
      if (result != oboe::Result::OK) {
        return result == oboe::Result::ErrorIllegalArgument && !id.empty()
                   ? AUD_IO_ERROR_NO_DEVICE
                   : AUD_IO_ERROR_DEVICE;
      }
      // The route's channels, at most two, unless the client asked.
      if (channels == 0 &&
          stream->getChannelCount() > int32_t(kDefaultChannels)) {
        stream->close();
        channelCount = int32_t(kDefaultChannels);
        continue;
      }
      *out = stream;
      return AUD_OK;
    }
    return AUD_IO_ERROR_DEVICE;
  }

  // [control] Fills the format from the open streams.
  void describe() {
    const std::shared_ptr<oboe::AudioStream>& main =
        output_ != nullptr ? output_ : input_;
    format_.sampleRate = main->getSampleRate();
    format_.outputChannels =
        output_ != nullptr ? uint32_t(output_->getChannelCount()) : 0;
    format_.inputChannels =
        input_ != nullptr ? uint32_t(input_->getChannelCount()) : 0;
    format_.bufferFrames = uint32_t(std::max(0, main->getBufferSizeInFrames()));
    format_.burstFrames = uint32_t(std::max(0, main->getFramesPerBurst()));
    format_.performanceMode = performanceModeOf(main->getPerformanceMode());
    format_.exclusive = main->getSharingMode() == oboe::SharingMode::Exclusive;
    const bool aaudio = main->getAudioApi() == oboe::AudioApi::AAudio;
    format_.timeSource =
        aaudio ? AUD_TIME_SOURCE_HARDWARE : AUD_TIME_SOURCE_ESTIMATED;
    if (output_ != nullptr) {
      format_.outputId = std::to_string(output_->getDeviceId());
    }
    if (input_ != nullptr) {
      format_.inputId = std::to_string(input_->getDeviceId());
    }
    std::string backend = aaudio ? "oboe/aaudio" : "oboe/opensles";
    if (aaudio && oboe::OboeExtensions::isMMapUsed(main.get())) {
      backend += "/mmap";
    }
    format_.backend = backend;
    nsPerFrame_ = 1e9 / format_.sampleRate;
  }

  // [realtime] The time of a callback: AAudio's timestamps moved to the
  // first frame of the block, otherwise an estimate from the buffer.
  DeviceTime timeOf(int32_t outputFrames, int32_t inputFrames) AUD_NONBLOCKING {
    DeviceTime time;
    time.callbackNs = aud_clock_now_ns();
    time.source = AUD_TIME_SOURCE_HARDWARE;
    bool estimated = false;
    if (output_ != nullptr) {
      // The block is not written yet: its first frame is the next one.
      const int64_t first = output_->getFramesWritten();
      const auto stamp = output_->getTimestamp(CLOCK_MONOTONIC);
      if (stamp) {
        time.outputNs = stamp.value().timestamp +
                        int64_t((first - stamp.value().position) * nsPerFrame_);
      } else {
        estimated = true;
        time.outputNs = time.callbackNs +
                        int64_t(output_->getBufferSizeInFrames() * nsPerFrame_);
      }
      time.outputLatencyFrames = uint32_t(std::max<int64_t>(
          0, int64_t((time.outputNs - time.callbackNs) / nsPerFrame_)));
    }
    if (input_ != nullptr) {
      // The block was read already: its first frame lies a block back.
      const int64_t first = input_->getFramesRead() - inputFrames;
      const auto stamp = input_->getTimestamp(CLOCK_MONOTONIC);
      if (stamp) {
        time.inputNs = stamp.value().timestamp +
                       int64_t((first - stamp.value().position) * nsPerFrame_);
      } else {
        estimated = true;
        time.inputNs = time.callbackNs -
                       int64_t(input_->getFramesPerBurst() * nsPerFrame_);
      }
      time.inputLatencyFrames = uint32_t(std::max<int64_t>(
          0, int64_t((time.callbackNs - time.inputNs) / nsPerFrame_)));
    }
    if (estimated) time.source = AUD_TIME_SOURCE_ESTIMATED;
    (void)outputFrames;
    return time;
  }

  AudIoStream* stream_;
  DeviceRequest request_;
  DeviceFormat format_;
  double nsPerFrame_ = 1e9 / 48000.0;
  bool running_ = false;
  std::shared_ptr<Callbacks> callbacks_;
  std::shared_ptr<Duplex> duplex_;
  std::shared_ptr<oboe::AudioStream> output_;
  std::shared_ptr<oboe::AudioStream> input_;
  std::unique_ptr<oboe::LatencyTuner> tuner_;
  std::vector<float> inputPad_;
};

oboe::DataCallbackResult Callbacks::onAudioReady(oboe::AudioStream* oboeStream,
                                                 void* audioData,
                                                 int32_t numFrames) {
  AndroidDevice* device = device_.load(std::memory_order_acquire);
  if (device == nullptr) return oboe::DataCallbackResult::Stop;
  if (oboeStream->getDirection() == oboe::Direction::Output) {
    device->outputReady(static_cast<float*>(audioData), numFrames);
  } else {
    device->inputReady(static_cast<const float*>(audioData), numFrames);
  }
  return oboe::DataCallbackResult::Continue;
}

oboe::ResultWithValue<int32_t> Duplex::readInput(int32_t numFrames) {
  AndroidDevice* device = device_.load(std::memory_order_acquire);
  if (device == nullptr) {
    return oboe::ResultWithValue<int32_t>(oboe::Result::ErrorClosed);
  }
  return device->readInput(numFrames);
}

oboe::DataCallbackResult Duplex::onBothStreamsReady(const void* inputData,
                                                    int numInputFrames,
                                                    void* outputData,
                                                    int numOutputFrames) {
  // The input is in the device's buffer already; see readInput.
  AndroidDevice* device = device_.load(std::memory_order_acquire);
  if (device == nullptr) return oboe::DataCallbackResult::Stop;
  device->duplexReady(numInputFrames, static_cast<float*>(outputData),
                      numOutputFrames);
  return oboe::DataCallbackResult::Continue;
}

class AndroidBackend : public Backend {
 public:
  const char* name() const override { return "oboe"; }

  // The devices, the permission and its request come from Java through
  // the Dart side.
  int32_t devices(std::vector<AudIoDevice>& out) override {
    return AUD_ERROR_UNSUPPORTED;
  }
  int32_t permission() override { return AUD_ERROR_UNSUPPORTED; }
  int32_t requestPermission() override { return AUD_ERROR_UNSUPPORTED; }

  std::unique_ptr<Device> open(const DeviceRequest& request,
                               AudIoStream* stream,
                               int32_t* result) override {
    auto device = std::make_unique<AndroidDevice>(stream, request);
    *result = device->open();
    if (*result != AUD_OK) return nullptr;
    return device;
  }
};

}  // namespace

std::unique_ptr<Backend> createPlatformBackend(
    AudIoSession* session, const AudIoSessionConfig& config) {
  return std::make_unique<AndroidBackend>();
}

}  // namespace aud_io

#endif  // __ANDROID__
