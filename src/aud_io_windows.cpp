// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The Windows backend (io-005, the Windows part of S3b): miniaudio's WASAPI
// device in shared mode on the endpoints of IMMDeviceEnumerator, which this
// file lists and observes. A stream names its endpoints by their id; one
// without an id opens the current default endpoint of the console role and
// reopens on the next one when the default changes, as on macOS. miniaudio
// is always given the id and its automatic stream routing is off, so that
// it never switches endpoints behind the stream's back. miniaudio, not
// Windows, converts a rate other than the mix format's, so that
// IAudioClient3 grants its low-latency period: the period asked for,
// rounded up to the engine's fundamental period. The events of an
// IMMNotificationClient - the endpoint list, the defaults, the state and the
// mix format of an endpoint - reach a COM thread of the backend, which also
// notices a miniaudio device that stopped on its own
// (AUDCLNT_E_DEVICE_INVALIDATED: a new format, a driver reset, another app
// in exclusive mode), and reach the streams through streamDeviceLost.
// miniaudio's callback carries no timestamp, so the host times are estimates
// as on macOS: the callback time plus the frames queued on the endpoint
// before the block and the stream latency of the audio client (time-001).
// What the backend decides about the endpoints - their routes, when a stream
// reopens, the period and the microphone permission - lives in wasapi:: of
// aud_io_internal.hpp, where the native tests reach it.

#if defined(_WIN32) && AUD_IO_WINDOWS_DEVICE

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI
#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio/miniaudio.h"

#include <windows.h>
// The property keys of the endpoints are defined in this file.
#include <initguid.h>
#include <propkeydef.h>
//
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "aud_clock.h"
#include "aud_io_internal.hpp"

namespace aud_io {
namespace {

using Microsoft::WRL::ComPtr;

constexpr double kFallbackRate = 48000;
// The low-latency default of the IO buffer: 256 frames at 48 kHz, 5.3 ms.
constexpr double kDefaultBufferSeconds = 256.0 / 48000.0;
constexpr const char* kBackendName = "miniaudio/WASAPI";
// How often the backend looks for a device that stopped on its own.
constexpr std::chrono::milliseconds kStopCheck{250};
// The privacy settings of the microphone.
constexpr const wchar_t* kMicrophoneConsent =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager"
    L"\\ConsentStore\\microphone";
constexpr const wchar_t* kMicrophoneConsentDesktop =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager"
    L"\\ConsentStore\\microphone\\NonPackaged";

// wasapi:: names the form factors by plain integers.
static_assert(wasapi::kRemoteNetworkDevice == RemoteNetworkDevice);
static_assert(wasapi::kSpeakers == Speakers);
static_assert(wasapi::kLineLevel == LineLevel);
static_assert(wasapi::kHeadphones == Headphones);
static_assert(wasapi::kMicrophone == Microphone);
static_assert(wasapi::kHeadset == Headset);
static_assert(wasapi::kHandset == Handset);
static_assert(wasapi::kUnknownDigitalPassthrough == UnknownDigitalPassthrough);
static_assert(wasapi::kSpdif == SPDIF);
static_assert(wasapi::kDigitalAudioDisplayDevice == DigitalAudioDisplayDevice);
static_assert(wasapi::kUnknownFormFactor == UnknownFormFactor);

// .............................................................................
// COM and the endpoints

// COM on the calling thread for a scope, multithreaded; a thread that runs
// in another apartment already keeps it.
class ComScope {
 public:
  ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComScope() {
    if (SUCCEEDED(result_)) CoUninitialize();
  }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

 private:
  HRESULT result_;
};

std::string utf8Of(const wchar_t* text) {
  if (text == nullptr || text[0] == L'\0') return std::string();
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return std::string();
  std::string out(size_t(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], size, nullptr, nullptr);
  out.resize(size_t(size - 1));
  return out;
}

std::wstring wideOf(const std::string& text) {
  if (text.empty()) return std::wstring();
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 1) return std::wstring();
  std::wstring out(size_t(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], size);
  out.resize(size_t(size - 1));
  return out;
}

ComPtr<IMMDeviceEnumerator> createEnumerator() {
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                              CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
    return nullptr;
  }
  return enumerator;
}

std::string idOf(IMMDevice* device) {
  LPWSTR id = nullptr;
  if (FAILED(device->GetId(&id))) return std::string();
  std::string out = utf8Of(id);
  CoTaskMemFree(id);
  return out;
}

ComPtr<IMMDevice> deviceWithId(IMMDeviceEnumerator* enumerator,
                               const std::string& id) {
  ComPtr<IMMDevice> device;
  if (id.empty() ||
      FAILED(enumerator->GetDevice(wideOf(id).c_str(), &device))) {
    return nullptr;
  }
  return device;
}

ComPtr<IMMDevice> defaultDevice(IMMDeviceEnumerator* enumerator, bool output) {
  ComPtr<IMMDevice> device;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(output ? eRender : eCapture,
                                                 eConsole, &device))) {
    return nullptr;
  }
  return device;
}

std::string defaultId(IMMDeviceEnumerator* enumerator, bool output) {
  ComPtr<IMMDevice> device = defaultDevice(enumerator, output);
  return device ? idOf(device.Get()) : std::string();
}

// The endpoint of a direction: the one with `id`, the default without one.
ComPtr<IMMDevice> resolve(IMMDeviceEnumerator* enumerator,
                          const std::string& id, bool output) {
  return id.empty() ? defaultDevice(enumerator, output)
                    : deviceWithId(enumerator, id);
}

// A property of an endpoint; VT_EMPTY when the endpoint has none.
class Property {
 public:
  Property(IPropertyStore* store, const PROPERTYKEY& key) {
    PropVariantInit(&value_);
    if (store != nullptr) store->GetValue(key, &value_);
  }
  ~Property() { PropVariantClear(&value_); }
  Property(const Property&) = delete;
  Property& operator=(const Property&) = delete;

  const PROPVARIANT& value() const { return value_; }

 private:
  PROPVARIANT value_;
};

std::string stringOf(IPropertyStore* store, const PROPERTYKEY& key) {
  const Property property(store, key);
  return property.value().vt == VT_LPWSTR ? utf8Of(property.value().pwszVal)
                                          : std::string();
}

uint32_t uintOf(IPropertyStore* store, const PROPERTYKEY& key,
                uint32_t fallback) {
  const Property property(store, key);
  return property.value().vt == VT_UI4 ? uint32_t(property.value().ulVal)
                                       : fallback;
}

// What the backend reads of an endpoint.
struct Endpoint {
  std::string id;
  std::string name;
  bool output = true;
  uint32_t route = AUD_IO_ROUTE_UNKNOWN;
  wasapi::EndpointState state;
};

Endpoint endpointOf(IMMDevice* device) {
  Endpoint endpoint;
  endpoint.id = idOf(device);
  ComPtr<IMMEndpoint> direction;
  EDataFlow flow = eRender;
  if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&direction)))) {
    direction->GetDataFlow(&flow);
  }
  endpoint.output = flow == eRender;
  DWORD state = 0;
  device->GetState(&state);
  endpoint.state.active = state == DEVICE_STATE_ACTIVE;
  ComPtr<IPropertyStore> store;
  if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) return endpoint;
  endpoint.name = stringOf(store.Get(), PKEY_Device_FriendlyName);
  // The mix format of the engine in shared mode.
  const Property mix(store.Get(), PKEY_AudioEngine_DeviceFormat);
  if (mix.value().vt == VT_BLOB &&
      mix.value().blob.cbSize >= sizeof(WAVEFORMATEX)) {
    const auto* format =
        reinterpret_cast<const WAVEFORMATEX*>(mix.value().blob.pBlobData);
    endpoint.state.rate = format->nSamplesPerSec;
    endpoint.state.channels = format->nChannels;
  }
  endpoint.route = wasapi::routeOf(
      uintOf(store.Get(), PKEY_AudioEndpoint_FormFactor,
             wasapi::kUnknownFormFactor),
      wasapi::busOf(stringOf(store.Get(), PKEY_Device_EnumeratorName)),
      endpoint.output);
  return endpoint;
}

// The state of the endpoint `id`; inactive when it is gone.
wasapi::EndpointState stateOf(IMMDeviceEnumerator* enumerator,
                              const std::string& id) {
  ComPtr<IMMDevice> device =
      enumerator != nullptr ? deviceWithId(enumerator, id) : nullptr;
  if (!device) {
    wasapi::EndpointState gone;
    gone.active = false;
    return gone;
  }
  return endpointOf(device.Get()).state;
}

// The period of `desired` frames that the engine of `device` grants a shared
// stream through IAudioClient3, as wasapi::periodOf rounds it.
uint32_t engineFramesOf(IMMDevice* device, uint32_t desired) {
  ComPtr<IAudioClient3> client;
  if (FAILED(device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr,
                              &client))) {
    return desired;
  }
  WAVEFORMATEX* mix = nullptr;
  if (FAILED(client->GetMixFormat(&mix))) return desired;
  UINT32 fallback = 0;
  UINT32 fundamental = 0;
  UINT32 minimum = 0;
  UINT32 maximum = 0;
  const HRESULT result = client->GetSharedModeEnginePeriod(
      mix, &fallback, &fundamental, &minimum, &maximum);
  CoTaskMemFree(mix);
  return SUCCEEDED(result)
             ? wasapi::periodOf(desired, fundamental, minimum, maximum)
             : desired;
}

// The value of one privacy setting of the microphone, empty when unset.
std::string consentOf(HKEY root, const wchar_t* key) {
  wchar_t value[32] = {0};
  DWORD size = sizeof(value);
  if (RegGetValueW(root, key, L"Value", RRF_RT_REG_SZ, nullptr, value,
                   &size) != ERROR_SUCCESS) {
    return std::string();
  }
  return utf8Of(value);
}

// .............................................................................
// The events of the endpoints

// What the notifications of the endpoints hand to the backend's thread.
struct Event {
  enum Kind { kList, kState, kDefault, kFormat };
  Kind kind = kList;
  // The endpoint; for kDefault the new default, empty while there is none.
  std::string id;
  bool output = true;  // kDefault: the default of the output or the input
};

// The queue between the notification client, which Windows calls on its own
// threads, and the backend's thread. It outlives both through shared
// ownership; once closed it drops what arrives.
class Events {
 public:
  void post(Event event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return;
    queue_.push_back(std::move(event));
    wake_.notify_one();
  }

  // [backend thread] Moves the queued events to `out`, waiting up to
  // `timeout` for one; false once closed.
  bool wait(std::deque<Event>& out, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait_for(lock, timeout,
                   [this] { return closed_ || !queue_.empty(); });
    if (closed_) return false;
    out.swap(queue_);
    return true;
  }

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    queue_.clear();
    wake_.notify_one();
  }

 private:
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Event> queue_;
  bool closed_ = false;
};

// Hands the endpoint events Windows reports to the backend's thread; does
// nothing else on Windows' threads, as the callbacks must return quickly.
class Notifications : public IMMNotificationClient {
 public:
  explicit Notifications(std::shared_ptr<Events> events)
      : events_(std::move(events)) {}

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG references = --references_;
    if (references == 0) delete this;
    return references;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
      *out = static_cast<IMMNotificationClient*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD) override {
    post(Event::kState, id);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override {
    post(Event::kList, id);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override {
    post(Event::kState, id);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                   LPCWSTR id) override {
    // The console role is the default device of the Windows settings.
    if (role != eConsole) return S_OK;
    Event event;
    event.kind = Event::kDefault;
    event.id = utf8Of(id);
    event.output = flow == eRender;
    events_->post(std::move(event));
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
      LPCWSTR id, const PROPERTYKEY key) override {
    if (IsEqualPropertyKey(key, PKEY_AudioEngine_DeviceFormat)) {
      post(Event::kFormat, id);
    }
    return S_OK;
  }

 private:
  void post(Event::Kind kind, LPCWSTR id) {
    Event event;
    event.kind = kind;
    event.id = utf8Of(id);
    events_->post(std::move(event));
  }

  std::atomic<ULONG> references_{1};
  std::shared_ptr<Events> events_;
};

// .............................................................................
// The device and the backend

class WinBackend;

class WinDevice : public Device {
 public:
  WinDevice(WinBackend* backend, AudIoStream* stream,
            const DeviceRequest& request)
      : backend_(backend), stream_(stream), request_(request) {}

  ~WinDevice() override;

  // [control] Opens the miniaudio device on `output` and `input`, either
  // null when the stream has no such direction; the engine of `first`, the
  // endpoint of the output or else of the input, rounds the period.
  int32_t open(ma_context* context, const Endpoint* output,
               const Endpoint* input, IMMDevice* first);

  int32_t start() override {
    primed_.store(false, std::memory_order_relaxed);
    if (ma_device_start(&device_) != MA_SUCCESS) return AUD_IO_ERROR_DEVICE;
    running_.store(true, std::memory_order_release);
    return AUD_OK;
  }

  int32_t stop() override {
    // Before miniaudio stops it, so that the stop is not taken for a loss.
    running_.store(false, std::memory_order_release);
    if (ma_device_is_started(&device_)) ma_device_stop(&device_);
    return AUD_OK;
  }

  const DeviceFormat& format() const override { return format_; }

  uint64_t xruns() const override {
    return xruns_.load(std::memory_order_relaxed);
  }

  const DeviceRequest& request() const { return request_; }

  // Whether the device uses the endpoint `id`.
  bool uses(const std::string& id) const {
    return (format_.outputChannels > 0 && format_.outputId == id) ||
           (format_.inputChannels > 0 && format_.inputId == id);
  }

  // Why the stream reopens now that the endpoint `id` is in the state
  // `now`, as wasapi::lossOf weighs it for every direction that uses it.
  uint32_t lossOf(const std::string& id,
                  const wasapi::EndpointState& now) const {
    if (format_.outputChannels > 0 && format_.outputId == id) {
      const uint32_t reason =
          wasapi::lossOf(now, true, outputRate_, request_, format_);
      if (reason != AUD_IO_REASON_NONE) return reason;
    }
    if (format_.inputChannels > 0 && format_.inputId == id) {
      return wasapi::lossOf(now, false, inputRate_, request_, format_);
    }
    return AUD_IO_REASON_NONE;
  }

  // [under the backend's mutex_] Whether miniaudio stopped the device on its
  // own: it runs for the stream, which did not stop it.
  bool stoppedOnItsOwn() {
    return ma_device_get_state(&device_) == ma_device_state_stopped &&
           running_.load(std::memory_order_acquire);
  }

  // [under the backend's mutex_] Reports the loss `reason` to the stream,
  // which drops it once it no longer runs on this device: one loss reaches
  // the backend more than once - the state of the endpoint, the new default
  // and the stopped device each tell of an unplug.
  void lost(uint32_t reason) {
    // Reported once as a device that stopped on its own.
    running_.store(false, std::memory_order_release);
    streamDeviceLost(stream_, *this, reason);
  }

  // [realtime] One callback of miniaudio.
  void process(void* output, const void* input,
               ma_uint32 frames) AUD_NONBLOCKING {
    DeviceTime time;
    time.callbackNs = aud_clock_now_ns();
    time.source = AUD_TIME_SOURCE_ESTIMATED;
    const double framesPerNs = format_.sampleRate / 1e9;
    const int64_t block = int64_t(std::llround(frames / framesPerNs));
    if (format_.outputChannels > 0) {
      // The block plays once the frames queued on the endpoint before it
      // have played; miniaudio hands it over after this callback.
      int64_t queued = block;
      ma_uint32 padding = 0;
      auto* client =
          static_cast<ma_IAudioClient*>(device_.wasapi.pAudioClientPlayback);
      if (ma_IAudioClient_GetCurrentPadding(client, &padding) == S_OK) {
        queued = int64_t(std::llround(padding * 1e9 /
                                      device_.playback.internalSampleRate));
        // An endpoint that ran dry since the last callback played silence;
        // before the first frames arrive it is empty anyway.
        if (padding == 0 && primed_.load(std::memory_order_relaxed)) {
          bump(xruns_, uint64_t{1});
        }
        if (padding > 0) primed_.store(true, std::memory_order_relaxed);
      }
      const int64_t latency =
          queued + outputNs_.load(std::memory_order_relaxed);
      time.outputNs = time.callbackNs + latency;
      time.outputLatencyFrames = uint32_t(std::llround(latency * framesPerNs));
    }
    if (format_.inputChannels > 0) {
      // The block was captured while the one before it was handed over.
      const int64_t latency = block + inputNs_.load(std::memory_order_relaxed);
      time.inputNs = time.callbackNs - latency;
      time.inputLatencyFrames = uint32_t(std::llround(latency * framesPerNs));
    }
    streamProcess(stream_, static_cast<const float*>(input),
                  static_cast<float*>(output), frames, time);
  }

 private:
  // [control] Reads the stream latencies of the audio clients for the audio
  // thread.
  void readLatencies() {
    MA_REFERENCE_TIME latency = 0;
    if (format_.outputChannels > 0 &&
        ma_IAudioClient_GetStreamLatency(
            static_cast<ma_IAudioClient*>(device_.wasapi.pAudioClientPlayback),
            &latency) == S_OK) {
      // In units of 100 ns.
      outputNs_.store(int64_t(latency) * 100, std::memory_order_relaxed);
    }
    latency = 0;
    if (format_.inputChannels > 0 &&
        ma_IAudioClient_GetStreamLatency(
            static_cast<ma_IAudioClient*>(device_.wasapi.pAudioClientCapture),
            &latency) == S_OK) {
      inputNs_.store(int64_t(latency) * 100, std::memory_order_relaxed);
    }
  }

  WinBackend* backend_;
  AudIoStream* stream_;
  DeviceRequest request_;
  DeviceFormat format_;
  ma_device device_{};
  bool initialized_ = false;
  // The mix rates of the endpoints when they opened.
  double outputRate_ = 0;
  double inputRate_ = 0;
  std::atomic<int64_t> outputNs_{0};
  std::atomic<int64_t> inputNs_{0};
  std::atomic<uint64_t> xruns_{0};
  // The stream started the device and has not stopped it.
  std::atomic<bool> running_{false};
  // The endpoint held frames since the start.
  std::atomic<bool> primed_{false};
};

void dataCallback(ma_device* device, void* output, const void* input,
                  ma_uint32 frames) AUD_NONBLOCKING {
  static_cast<WinDevice*>(device->pUserData)->process(output, input, frames);
}

class WinBackend : public Backend {
 public:
  explicit WinBackend(AudIoSession* session)
      : session_(session), events_(std::make_shared<Events>()) {}

  ~WinBackend() override {
    events_->close();
    if (thread_.joinable()) thread_.join();
    if (contextReady_) ma_context_uninit(&context_);
  }

  // [control] Starts miniaudio and the thread that observes the endpoints.
  bool init() {
    ma_context_config contextConfig = ma_context_config_init();
    const ma_backend backends[] = {ma_backend_wasapi};
    if (ma_context_init(backends, 1, &contextConfig, &context_) !=
        MA_SUCCESS) {
      return false;
    }
    contextReady_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
  }

  const char* name() const override { return kBackendName; }

  int32_t devices(std::vector<AudIoDevice>& out) override {
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator = createEnumerator();
    if (!enumerator) return AUD_IO_ERROR_DEVICE;
    const std::string defaultOutput = defaultId(enumerator.Get(), true);
    const std::string defaultInput = defaultId(enumerator.Get(), false);
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE,
                                              &collection))) {
      return AUD_OK;
    }
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      ComPtr<IMMDevice> item;
      if (FAILED(collection->Item(i, &item))) continue;
      const Endpoint endpoint = endpointOf(item.Get());
      if (endpoint.id.empty()) continue;
      AudIoDevice device{};
      device.struct_size = sizeof(AudIoDevice);
      device.directions = endpoint.output ? AUD_IO_OUTPUT : AUD_IO_INPUT;
      device.route = endpoint.route;
      if (endpoint.output && endpoint.id == defaultOutput) {
        device.flags |= AUD_IO_DEVICE_DEFAULT_OUTPUT | AUD_IO_DEVICE_ACTIVE;
      }
      if (!endpoint.output && endpoint.id == defaultInput) {
        device.flags |= AUD_IO_DEVICE_DEFAULT_INPUT | AUD_IO_DEVICE_ACTIVE;
      }
      if (inUse(endpoint.id)) device.flags |= AUD_IO_DEVICE_ACTIVE;
      (endpoint.output ? device.max_output_channels
                       : device.max_input_channels) = endpoint.state.channels;
      // The rate of the mix format; miniaudio converts any other.
      if (endpoint.state.rate > 0) {
        device.num_sample_rates = 1;
        device.sample_rates[0] = endpoint.state.rate;
      }
      copyString(device.id, sizeof(device.id), endpoint.id);
      copyString(device.name, sizeof(device.name), endpoint.name);
      out.push_back(device);
    }
    return AUD_OK;
  }

  int32_t permission() override {
    return wasapi::permissionOf({
        consentOf(HKEY_LOCAL_MACHINE, kMicrophoneConsent),
        consentOf(HKEY_CURRENT_USER, kMicrophoneConsent),
        consentOf(HKEY_CURRENT_USER, kMicrophoneConsentDesktop),
    });
  }

  int32_t requestPermission() override {
    // Windows asks desktop apps nothing: the answer is the setting.
    sessionPermissionChanged(session_, permission());
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
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator = createEnumerator();
    if (!enumerator) {
      *result = AUD_IO_ERROR_DEVICE;
      return nullptr;
    }
    ComPtr<IMMDevice> outputDevice =
        output ? resolve(enumerator.Get(), request.outputId, true) : nullptr;
    ComPtr<IMMDevice> inputDevice =
        input ? resolve(enumerator.Get(), request.inputId, false) : nullptr;
    Endpoint outputEndpoint;
    Endpoint inputEndpoint;
    if (outputDevice) outputEndpoint = endpointOf(outputDevice.Get());
    if (inputDevice) inputEndpoint = endpointOf(inputDevice.Get());
    if ((output && (!outputDevice || !outputEndpoint.output ||
                    !outputEndpoint.state.active)) ||
        (input && (!inputDevice || inputEndpoint.output ||
                   !inputEndpoint.state.active))) {
      *result = AUD_IO_ERROR_NO_DEVICE;
      return nullptr;
    }
    auto device = std::make_unique<WinDevice>(this, stream, request);
    *result = device->open(&context_, output ? &outputEndpoint : nullptr,
                           input ? &inputEndpoint : nullptr,
                           output ? outputDevice.Get() : inputDevice.Get());
    if (*result != AUD_OK) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.push_back(device.get());
    return device;
  }

  // [control] A device closes.
  void remove(WinDevice* device) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.erase(std::remove(devices_.begin(), devices_.end(), device),
                   devices_.end());
  }

 private:
  // [backend thread] Observes the endpoints until the backend closes.
  void run() {
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator = createEnumerator();
    auto* notifications = new Notifications(events_);
    const bool registered =
        enumerator &&
        SUCCEEDED(
            enumerator->RegisterEndpointNotificationCallback(notifications));
    std::deque<Event> events;
    while (events_->wait(events, kStopCheck)) {
      for (const Event& event : events) handle(enumerator.Get(), event);
      events.clear();
      checkStopped(enumerator.Get());
    }
    if (registered) {
      enumerator->UnregisterEndpointNotificationCallback(notifications);
    }
    notifications->Release();
  }

  // [backend thread] An endpoint came or went, changed its state or its mix
  // format, or a default changed.
  void handle(IMMDeviceEnumerator* enumerator, const Event& event) {
    if (event.kind == Event::kDefault) {
      // Under the lock: a stream lives at least as long as its device is
      // registered, and the stream functions take no lock of the stream's
      // control.
      std::lock_guard<std::mutex> lock(mutex_);
      for (WinDevice* device : devices_) {
        // A stream without an id follows the default endpoint.
        if (wasapi::movesToDefault(event.output, device->request(),
                                   device->format(), event.id)) {
          device->lost(AUD_IO_REASON_ROUTE_OVERRIDE);
        }
      }
    } else if (event.kind != Event::kList) {
      const wasapi::EndpointState now = stateOf(enumerator, event.id);
      std::lock_guard<std::mutex> lock(mutex_);
      for (WinDevice* device : devices_) {
        const uint32_t reason = device->lossOf(event.id, now);
        if (reason != AUD_IO_REASON_NONE) device->lost(reason);
      }
    }
    sessionDevicesChanged(session_);
  }

  // [backend thread] Reports the devices miniaudio stopped on its own, for
  // the reason the state of their endpoints tells, else as an error.
  void checkStopped(IMMDeviceEnumerator* enumerator) {
    std::vector<std::string> ids;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (WinDevice* device : devices_) {
        if (!device->stoppedOnItsOwn()) continue;
        const DeviceFormat& format = device->format();
        if (format.outputChannels > 0) ids.push_back(format.outputId);
        if (format.inputChannels > 0) ids.push_back(format.inputId);
      }
    }
    if (ids.empty()) return;
    // The endpoints are read outside the lock, which a closing device waits
    // for.
    std::vector<std::pair<std::string, wasapi::EndpointState>> states;
    for (const std::string& id : ids) {
      states.emplace_back(id, stateOf(enumerator, id));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (WinDevice* device : devices_) {
      if (!device->stoppedOnItsOwn()) continue;
      uint32_t reason = AUD_IO_REASON_NONE;
      for (const auto& state : states) {
        if (reason == AUD_IO_REASON_NONE) {
          reason = device->lossOf(state.first, state.second);
        }
      }
      device->lost(reason != AUD_IO_REASON_NONE ? reason : AUD_IO_REASON_ERROR);
    }
  }

  bool inUse(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (WinDevice* device : devices_) {
      if (device->uses(id)) return true;
    }
    return false;
  }

  AudIoSession* session_;
  ma_context context_{};
  bool contextReady_ = false;
  std::shared_ptr<Events> events_;
  std::thread thread_;
  std::mutex mutex_;
  std::vector<WinDevice*> devices_;
};

WinDevice::~WinDevice() {
  backend_->remove(this);
  if (initialized_) ma_device_uninit(&device_);
}

int32_t WinDevice::open(ma_context* context, const Endpoint* output,
                        const Endpoint* input, IMMDevice* first) {
  const bool hasOutput = output != nullptr;
  const bool hasInput = input != nullptr;
  ma_device_config config = ma_device_config_init(
      hasOutput && hasInput ? ma_device_type_duplex
      : hasOutput           ? ma_device_type_playback
                            : ma_device_type_capture);
  // The endpoint ids, also of the defaults: miniaudio follows a default of
  // its own accord when it gets none, and the stream reopens instead.
  ma_device_id outputId{};
  ma_device_id inputId{};
  if (hasOutput) {
    const std::wstring id = wideOf(output->id);
    id.copy(outputId.wasapi, std::size(outputId.wasapi) - 1);
    config.playback.pDeviceID = &outputId;
  }
  if (hasInput) {
    const std::wstring id = wideOf(input->id);
    id.copy(inputId.wasapi, std::size(inputId.wasapi) - 1);
    config.capture.pDeviceID = &inputId;
  }
  config.playback.format = ma_format_f32;
  config.playback.channels =
      request_.outputChannels > 0
          ? request_.outputChannels
          : hasOutput && output->state.channels > 0
              ? std::min(output->state.channels, kDefaultChannels)
              : kDefaultChannels;
  config.capture.format = ma_format_f32;
  config.capture.channels =
      request_.inputChannels > 0
          ? request_.inputChannels
          : hasInput && input->state.channels > 0
              ? std::min(input->state.channels, kDefaultChannels)
              : kDefaultChannels;
  // The rate of the mix format unless the stream asks for one; miniaudio
  // converts then, never Windows, which would rule out IAudioClient3. The
  // rates of both endpoints tell later whether their own rate changed
  // (wasapi::lossOf).
  outputRate_ = hasOutput ? output->state.rate : 0;
  inputRate_ = hasInput ? input->state.rate : 0;
  const double hardware = hasOutput ? outputRate_ : inputRate_;
  const double rate = request_.sampleRate > 0 ? request_.sampleRate
                      : hardware > 0          ? hardware
                                              : kFallbackRate;
  config.sampleRate = ma_uint32(std::lround(rate));
  uint32_t period = request_.bufferFrames > 0
                        ? request_.bufferFrames
                        : uint32_t(std::lround(rate * kDefaultBufferSeconds));
  // The engine's period applies at its own rate only.
  if (std::fabs(rate - hardware) < 0.5) period = engineFramesOf(first, period);
  config.periodSizeInFrames = period;
  const bool lowLatency =
      request_.performanceMode != AUD_IO_PERFORMANCE_POWER_SAVING;
  config.performanceProfile = lowLatency ? ma_performance_profile_low_latency
                                         : ma_performance_profile_conservative;
  config.wasapi.usage =
      lowLatency ? ma_wasapi_usage_pro_audio : ma_wasapi_usage_default;
  config.wasapi.noAutoConvertSRC = MA_TRUE;
  config.wasapi.noAutoStreamRouting = MA_TRUE;
  // Callbacks of the size the engine asks for; the stream splits them.
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
  format_.bufferFrames = hasOutput
                             ? device_.wasapi.actualBufferSizeInFramesPlayback
                             : device_.wasapi.actualBufferSizeInFramesCapture;
  format_.burstFrames = hasOutput ? device_.playback.internalPeriodSizeInFrames
                                  : device_.capture.internalPeriodSizeInFrames;
  format_.performanceMode = request_.performanceMode;
  format_.exclusive = false;
  format_.timeSource = AUD_TIME_SOURCE_ESTIMATED;
  if (hasOutput) format_.outputId = output->id;
  if (hasInput) format_.inputId = input->id;
  format_.backend = kBackendName;
  readLatencies();
  return AUD_OK;
}

}  // namespace

std::unique_ptr<Backend> createPlatformBackend(
    AudIoSession* session, const AudIoSessionConfig& config) {
  auto backend = std::make_unique<WinBackend>(session);
  if (!backend->init()) return nullptr;
  return backend;
}

}  // namespace aud_io

#endif  // defined(_WIN32) && AUD_IO_WINDOWS_DEVICE
