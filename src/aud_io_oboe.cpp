// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The Oboe backend for Android (io-002): AAudio with the OpenSL ES fallback,
// low-latency performance mode, exclusive sharing, float output, xrun count
// and latency estimate. The spike does not reopen a disconnected stream;
// the route-change sequence of lifecycle-001 is ticket S3.

#include <memory>
#include <new>
#include <string>

#include "aud_audio_io.h"
#include "aud_io_stats.hpp"
#include "oboe/Oboe.h"

struct AudIoStream : public oboe::AudioStreamDataCallback,
                     public oboe::AudioStreamErrorCallback {
  std::shared_ptr<oboe::AudioStream> stream;
  AudRenderCallback render = nullptr;
  void* user = nullptr;
  uint32_t channels = 0;
  bool started = false;
  AudIoStatsCollector stats;
  std::string backendName = "oboe";

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream*, void* audioData,
                                        int32_t numFrames) override {
    const uint32_t frames = static_cast<uint32_t>(numFrames);
    const int64_t start = stats.begin(frames);
    render(user, static_cast<float*>(audioData), frames, channels);
    stats.end(start);
    return oboe::DataCallbackResult::Continue;
  }

  void onErrorAfterClose(oboe::AudioStream*, oboe::Result) override {
    stats.disconnects.fetch_add(1, std::memory_order_relaxed);
    started = false;
  }
};

AUD_EXPORT AudIoStream* aud_io_open(const AudIoConfig* config,
                                    AudRenderCallback render, void* user) {
  if (config == nullptr || config->struct_size < sizeof(AudIoConfig) ||
      config->channels == 0 || render == nullptr) {
    return nullptr;
  }
  auto* stream = new (std::nothrow) AudIoStream();
  if (stream == nullptr) return nullptr;
  stream->render = render;
  stream->user = user;
  stream->channels = config->channels;
  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Output)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setSharingMode(oboe::SharingMode::Exclusive)
      ->setFormat(oboe::AudioFormat::Float)
      ->setChannelCount(static_cast<int32_t>(config->channels))
      ->setDataCallback(stream)
      ->setErrorCallback(stream);
  if (config->sample_rate > 0) {
    builder.setSampleRate(static_cast<int32_t>(config->sample_rate));
  }
  if (config->frames_per_callback > 0) {
    builder.setFramesPerDataCallback(static_cast<int32_t>(config->frames_per_callback));
  }
  if (builder.openStream(stream->stream) != oboe::Result::OK) {
    delete stream;
    return nullptr;
  }
  stream->stats.sampleRate = stream->stream->getSampleRate();
  stream->backendName =
      stream->stream->getAudioApi() == oboe::AudioApi::AAudio ? "oboe/aaudio"
                                                              : "oboe/opensles";
  if (stream->stream->getPerformanceMode() != oboe::PerformanceMode::LowLatency) {
    stream->backendName += " (no low latency)";
  }
  return stream;
}

AUD_EXPORT int32_t aud_io_start(AudIoStream* stream) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  if (stream->started) return AUD_ERROR_STATE;
  stream->stats.lastStart = 0;
  if (stream->stream->requestStart() != oboe::Result::OK) return AUD_ERROR_FAILED;
  stream->started = true;
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_stop(AudIoStream* stream) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  if (!stream->started) return AUD_ERROR_STATE;
  stream->started = false;
  return stream->stream->requestStop() == oboe::Result::OK ? AUD_OK : AUD_ERROR_FAILED;
}

AUD_EXPORT void aud_io_close(AudIoStream* stream) {
  if (stream == nullptr) return;
  if (stream->stream) stream->stream->close();
  delete stream;
}

AUD_EXPORT double aud_io_sample_rate(AudIoStream* stream) {
  return stream == nullptr ? 0.0 : static_cast<double>(stream->stream->getSampleRate());
}

AUD_EXPORT uint32_t aud_io_channels(AudIoStream* stream) {
  return stream == nullptr ? 0 : stream->channels;
}

AUD_EXPORT uint32_t aud_io_frames_per_callback(AudIoStream* stream) {
  if (stream == nullptr) return 0;
  const int32_t perCallback = stream->stream->getFramesPerDataCallback();
  const int32_t frames =
      perCallback > 0 ? perCallback : stream->stream->getFramesPerBurst();
  return frames > 0 ? static_cast<uint32_t>(frames) : 0;
}

AUD_EXPORT const char* aud_io_backend_name(AudIoStream* stream) {
  return stream == nullptr ? "" : stream->backendName.c_str();
}

AUD_EXPORT void aud_io_get_stats(AudIoStream* stream, AudIoStats* stats) {
  if (stream == nullptr || stats == nullptr || stats->struct_size < sizeof(AudIoStats)) {
    return;
  }
  stream->stats.copyTo(stats);
  const auto xruns = stream->stream->getXRunCount();
  if (xruns) stats->xruns = static_cast<uint64_t>(xruns.value());
  const auto latency = stream->stream->calculateLatencyMillis();
  if (latency) stats->output_latency_ms = latency.value();
}

AUD_EXPORT void aud_io_reset_stats(AudIoStream* stream) {
  if (stream != nullptr) stream->stats.reset();
}
