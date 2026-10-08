// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The miniaudio backend: Core Audio on macOS and iOS, WASAPI on Windows,
// PulseAudio and ALSA on Linux, plus the null device for tests. The device
// layer only; miniaudio's decoding, engine and node graph stay out. This
// file is Objective-C++ because miniaudio configures AVAudioSession on iOS.

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_NULL
#if defined(__APPLE__)
#define MA_NO_RUNTIME_LINKING
#define MA_ENABLE_COREAUDIO
#elif defined(_WIN32)
#define MA_ENABLE_WASAPI
#else
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_ALSA
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio/miniaudio.h"

#include <cstdio>
#include <new>

#include "aud_audio_io.h"
#include "aud_io_stats.hpp"

struct AudIoStream {
  ma_context context{};
  ma_device device{};
  bool contextReady = false;
  bool deviceReady = false;
  AudRenderCallback render = nullptr;
  void* user = nullptr;
  uint32_t channels = 0;
  AudIoStatsCollector stats;
  char backendName[64] = "miniaudio";
};

namespace {

void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
  auto* stream = static_cast<AudIoStream*>(device->pUserData);
  const int64_t start = stream->stats.begin(frames);
  stream->render(stream->user, static_cast<float*>(output), frames,
                 stream->channels);
  stream->stats.end(start);
}

bool initContext(AudIoStream* stream, bool useNullBackend) {
  ma_context_config config = ma_context_config_init();
#if defined(__APPLE__)
  config.coreaudio.sessionCategory = ma_ios_session_category_playback;
#endif
  const ma_backend nullBackend[] = {ma_backend_null};
  const ma_result result =
      useNullBackend ? ma_context_init(nullBackend, 1, &config, &stream->context)
                     : ma_context_init(nullptr, 0, &config, &stream->context);
  if (result != MA_SUCCESS) return false;
  stream->contextReady = true;
  std::snprintf(stream->backendName, sizeof(stream->backendName), "miniaudio/%s",
                ma_get_backend_name(stream->context.backend));
  return true;
}

bool initDevice(AudIoStream* stream, const AudIoConfig* config) {
  ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
  deviceConfig.playback.format = ma_format_f32;
  deviceConfig.playback.channels = config->channels;
  deviceConfig.sampleRate = static_cast<ma_uint32>(config->sample_rate);
  deviceConfig.periodSizeInFrames = config->frames_per_callback;
  deviceConfig.performanceProfile = ma_performance_profile_low_latency;
  deviceConfig.dataCallback = dataCallback;
  deviceConfig.pUserData = stream;
  if (ma_device_init(&stream->context, &deviceConfig, &stream->device) != MA_SUCCESS) {
    return false;
  }
  stream->deviceReady = true;
  stream->stats.sampleRate = stream->device.sampleRate;
  return true;
}

}  // namespace

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
  if (!initContext(stream, config->use_null_backend != 0) ||
      !initDevice(stream, config)) {
    aud_io_close(stream);
    return nullptr;
  }
  return stream;
}

AUD_EXPORT int32_t aud_io_start(AudIoStream* stream) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  if (ma_device_is_started(&stream->device)) return AUD_ERROR_STATE;
  stream->stats.lastStart = 0;
  return ma_device_start(&stream->device) == MA_SUCCESS ? AUD_OK : AUD_ERROR_FAILED;
}

AUD_EXPORT int32_t aud_io_stop(AudIoStream* stream) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  if (!ma_device_is_started(&stream->device)) return AUD_ERROR_STATE;
  return ma_device_stop(&stream->device) == MA_SUCCESS ? AUD_OK : AUD_ERROR_FAILED;
}

AUD_EXPORT void aud_io_close(AudIoStream* stream) {
  if (stream == nullptr) return;
  if (stream->deviceReady) ma_device_uninit(&stream->device);
  if (stream->contextReady) ma_context_uninit(&stream->context);
  delete stream;
}

AUD_EXPORT double aud_io_sample_rate(AudIoStream* stream) {
  return stream == nullptr ? 0.0 : static_cast<double>(stream->device.sampleRate);
}

AUD_EXPORT uint32_t aud_io_channels(AudIoStream* stream) {
  return stream == nullptr ? 0 : stream->channels;
}

AUD_EXPORT uint32_t aud_io_frames_per_callback(AudIoStream* stream) {
  return stream == nullptr ? 0 : stream->device.playback.internalPeriodSizeInFrames;
}

AUD_EXPORT const char* aud_io_backend_name(AudIoStream* stream) {
  return stream == nullptr ? "" : stream->backendName;
}

AUD_EXPORT void aud_io_get_stats(AudIoStream* stream, AudIoStats* stats) {
  if (stream == nullptr || stats == nullptr || stats->struct_size < sizeof(AudIoStats)) {
    return;
  }
  stream->stats.copyTo(stats);
}

AUD_EXPORT void aud_io_reset_stats(AudIoStream* stream) {
  if (stream != nullptr) stream->stats.reset();
}
