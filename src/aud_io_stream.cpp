// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The session and the stream of aud_audio_io, shared by every backend: the
// realtime callback that splits and de-interleaves the device blocks and
// calls the render function with an AudStreamTime (time-001), the counters,
// the notification path to the control thread (interop-001) and the worker
// that recovers a stream from disconnects, interruptions and format changes
// (lifecycle-001).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

#include "aud_clock.h"
#include "aud_io_internal.hpp"
#include "aud_semaphore.hpp"
#include "aud_spsc_queue.hpp"

namespace aud_io {
namespace {

// The notifications the audio thread of one stream can post between two
// takes of the control thread.
constexpr size_t kRealtimeQueueCapacity = 64;

// A recovery gives up after this long and reports AUD_IO_NOTIFY_FAILED,
// unless the session says otherwise.
constexpr uint32_t kDefaultRecoveryTimeoutMs = 5000;

// The waits between two attempts to reopen a device; the last repeats.
constexpr int64_t kBackoffMs[] = {0, 10, 20, 50, 100, 200, 400};

// The weight of a new deviation in the mean deviation of the host times.
constexpr double kJitterWeight = 1.0 / 16.0;

// A callback is late when the time since the previous one exceeds the
// previous block by this factor.
constexpr double kLateFactor = 1.5;

// A notification with its place in the order of the session.
struct Pending {
  uint64_t sequence = 0;
  AudIoNotification notification{};
};

// The counters of a stream. The audio thread is the only writer of the
// callback counters, the worker and the control thread of the others.
struct Counters {
  std::atomic<uint64_t> callbacks{0};
  std::atomic<uint64_t> frames{0};
  std::atomic<uint64_t> renders{0};
  std::atomic<uint32_t> callbackFramesMin{UINT32_MAX};
  std::atomic<uint32_t> callbackFramesMax{0};
  std::atomic<int64_t> periodMin{INT64_MAX};
  std::atomic<int64_t> periodMax{0};
  std::atomic<int64_t> periodSum{0};
  std::atomic<uint64_t> periodCount{0};
  std::atomic<uint64_t> late{0};
  std::atomic<uint64_t> disconnects{0};
  std::atomic<uint64_t> recoveries{0};
  std::atomic<uint64_t> interruptions{0};
  std::atomic<uint64_t> held{0};
  std::atomic<uint64_t> renderErrors{0};
  std::atomic<uint64_t> dropped{0};
  std::atomic<int64_t> callbackMax{0};
  std::atomic<int64_t> callbackSum{0};
  std::atomic<int64_t> recoveryMax{0};
  std::atomic<int64_t> recoveryLast{0};
  std::atomic<int64_t> jitterMax{0};

  void reset() {
    callbacks = 0;
    frames = 0;
    renders = 0;
    callbackFramesMin = UINT32_MAX;
    callbackFramesMax = 0;
    periodMin = INT64_MAX;
    periodMax = 0;
    periodSum = 0;
    periodCount = 0;
    late = 0;
    disconnects = 0;
    recoveries = 0;
    interruptions = 0;
    held = 0;
    renderErrors = 0;
    dropped = 0;
    callbackMax = 0;
    callbackSum = 0;
    recoveryMax = 0;
    recoveryLast = 0;
    jitterMax = 0;
  }
};

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

// The stream time of the last block: the audio thread writes it, the
// control thread reads it; a sequence lock over atomic fields.
class TimeCell {
 public:
  void store(const AudStreamTime& time) AUD_NONBLOCKING {
    const uint32_t sequence = sequence_.load(std::memory_order_relaxed);
    sequence_.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    frames_.store(time.frames, std::memory_order_relaxed);
    sampleRate_.store(time.sample_rate, std::memory_order_relaxed);
    position_.store(time.sample_position, std::memory_order_relaxed);
    hostNs_.store(time.host_time_ns, std::memory_order_relaxed);
    source_.store(time.host_time_source, std::memory_order_relaxed);
    accuracyNs_.store(time.host_time_accuracy_ns, std::memory_order_relaxed);
    outputLatency_.store(time.output_latency_frames,
                         std::memory_order_relaxed);
    inputLatency_.store(time.input_latency_frames, std::memory_order_relaxed);
    sequence_.store(sequence + 2, std::memory_order_release);
  }

  AudStreamTime load() const {
    AudStreamTime time{};
    while (true) {
      const uint32_t before = sequence_.load(std::memory_order_acquire);
      time.struct_size = sizeof(AudStreamTime);
      time.frames = frames_.load(std::memory_order_relaxed);
      time.sample_rate = sampleRate_.load(std::memory_order_relaxed);
      time.sample_position = position_.load(std::memory_order_relaxed);
      time.host_time_ns = hostNs_.load(std::memory_order_relaxed);
      time.host_time_source = source_.load(std::memory_order_relaxed);
      time.host_time_accuracy_ns = accuracyNs_.load(std::memory_order_relaxed);
      time.output_latency_frames =
          outputLatency_.load(std::memory_order_relaxed);
      time.input_latency_frames = inputLatency_.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      const uint32_t after = sequence_.load(std::memory_order_relaxed);
      if (before == after && (before & 1u) == 0) return time;
      std::this_thread::yield();
    }
  }

 private:
  std::atomic<uint32_t> sequence_{0};
  std::atomic<uint32_t> frames_{0};
  std::atomic<double> sampleRate_{0.0};
  std::atomic<int64_t> position_{0};
  std::atomic<int64_t> hostNs_{0};
  std::atomic<uint32_t> source_{0};
  std::atomic<int64_t> accuracyNs_{0};
  std::atomic<uint32_t> outputLatency_{0};
  std::atomic<uint32_t> inputLatency_{0};
};

// What the audio thread owns while the device runs. The control thread and
// the worker change it only while no callback runs: before a start, after a
// stop, while the device is closed.
struct Realtime {
  AudRenderFunction render = nullptr;
  void* user = nullptr;
  double sampleRate = 0;
  uint32_t maxFrames = 0;
  uint32_t outputChannels = 0;
  uint32_t inputChannels = 0;
  std::vector<float> outputPlanes;
  std::vector<float> inputPlanes;
  std::vector<float*> outputPointers;
  std::vector<float*> inputPointers;
  AudAudioBus outputBus{};
  AudAudioBus inputBus{};
  int64_t position = 0;    // the sample position of the next block
  int64_t nextHostNs = 0;  // where the next block starts in host time
  int64_t lastCallbackNs = 0;
  uint32_t lastCallbackFrames = 0;
  double jitterNs = 0;     // the mean deviation of the host times
  bool hasJitter = false;
  bool restarted = false;  // the next callback is the first after a start
  int64_t causeNs = 0;     // when the device was lost; 0 after a start
  bool renderFailing = false;
};

int64_t nowNs() { return aud_clock_now_ns(); }

}  // namespace
}  // namespace aud_io

using aud_io::Counters;
using aud_io::Pending;
using aud_io::Realtime;
using aud_io::TimeCell;

struct AudIoSession {
  AudIoSessionConfig config{};
  std::unique_ptr<aud_io::Backend> backend;

  // Notifications posted off the audio thread; the audio threads post into
  // the queue of their stream. A sequence number orders both.
  std::atomic<uint64_t> sequence{0};
  std::mutex queueMutex;
  std::deque<Pending> queue;
  size_t capacity = aud_io::kDefaultNotificationCapacity;
  std::atomic<uint64_t> dropped{0};
  int64_t giveUpNs = 0;  // how long a recovery tries

  std::mutex streamsMutex;
  std::vector<AudIoStream*> streams;
  int32_t nextStreamId = 1;

  // The notification thread: the audio thread posts `wake`, the thread calls
  // the listener.
  AudSemaphore wake;
  std::atomic<bool> wakePending{false};
  std::atomic<bool> stopNotifier{false};
  std::mutex listenerMutex;
  AudIoListener listener = nullptr;
  void* listenerUser = nullptr;
  std::thread notifier;
};

struct AudIoStream {
  explicit AudIoStream(AudIoSession* owner)
      : session(owner), realtimeQueue(aud_io::kRealtimeQueueCapacity) {}

  AudIoSession* session;
  int32_t id = 0;
  uint32_t direction = AUD_IO_OUTPUT;
  aud_io::DeviceRequest request;

  // Guarded by `control`: the control thread and the worker take turns on
  // the device.
  std::mutex control;
  std::unique_ptr<aud_io::Device> device;
  aud_io::DeviceFormat format;
  bool opened = false;  // a device was open once; formats compare from then
  bool interrupted = false;
  uint64_t xrunBase = 0;   // the xruns of closed devices
  uint64_t xrunReset = 0;  // the xruns at the last reset
  // The xruns as last read, for a read while the worker holds the device.
  std::atomic<uint64_t> xrunsSeen{0};
  // The session's dropped notifications at the last reset of the counters.
  std::atomic<uint64_t> sessionDroppedReset{0};

  Realtime rt;
  Counters counters;
  TimeCell lastTime;
  AudSpscQueue<Pending> realtimeQueue;
  std::atomic<int32_t> state{AUD_IO_STATE_STOPPED};
  std::atomic<bool> wantRunning{false};
  std::atomic<uint32_t> generation{1};
  std::atomic<uint32_t> acknowledged{1};
  std::atomic<bool> resetPeriod{false};

  // The worker and its requests, guarded by `workerMutex`.
  std::mutex workerMutex;
  std::condition_variable workerWake;
  bool recoverPending = false;
  uint32_t recoverReason = AUD_IO_REASON_NONE;
  int64_t recoverCauseNs = 0;
  bool interruptBeginPending = false;
  bool interruptEndPending = false;
  uint32_t interruptReason = AUD_IO_REASON_NONE;
  bool routePending = false;
  uint32_t routeReason = AUD_IO_REASON_NONE;
  std::atomic<bool> closing{false};
  std::thread worker;
};

namespace aud_io {
namespace {

// ............................................................................
// Notifications

void wakeNotifier(AudIoSession* session) AUD_NONBLOCKING {
  if (!session->wakePending.exchange(true, std::memory_order_acq_rel)) {
    session->wake.post();
  }
}

AudIoNotification makeNotification(uint32_t type, int32_t stream,
                                    uint32_t reason) {
  AudIoNotification notification{};
  notification.struct_size = sizeof(AudIoNotification);
  notification.type = type;
  notification.stream = stream;
  notification.reason = reason;
  notification.host_time_ns = nowNs();
  return notification;
}

// [any thread but the realtime thread]
void post(AudIoSession* session, const AudIoNotification& notification) {
  Pending pending;
  pending.sequence = session->sequence.fetch_add(1, std::memory_order_relaxed);
  pending.notification = notification;
  {
    std::lock_guard<std::mutex> lock(session->queueMutex);
    if (session->queue.size() >= session->capacity) {
      session->dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    session->queue.push_back(pending);
  }
  wakeNotifier(session);
}

// Fills the format fields of a notification of `stream`; the control lock
// is held.
AudIoNotification streamNotification(AudIoStream* stream, uint32_t type,
                                     uint32_t reason) {
  AudIoNotification notification = makeNotification(type, stream->id, reason);
  notification.generation = stream->generation.load(std::memory_order_relaxed);
  notification.sample_rate = stream->format.sampleRate;
  notification.output_channels = stream->format.outputChannels;
  notification.input_channels = stream->format.inputChannels;
  return notification;
}

// [realtime]
void postRealtime(AudIoStream* stream, uint32_t type, int32_t code,
                  int64_t value, int64_t hostNs) AUD_NONBLOCKING {
  Pending pending;
  pending.sequence =
      stream->session->sequence.fetch_add(1, std::memory_order_relaxed);
  AudIoNotification& notification = pending.notification;
  notification.struct_size = sizeof(AudIoNotification);
  notification.type = type;
  notification.stream = stream->id;
  notification.code = code;
  notification.reason = AUD_IO_REASON_NONE;
  notification.generation =
      stream->generation.load(std::memory_order_relaxed);
  notification.host_time_ns = hostNs;
  notification.value = value;
  notification.sample_rate = stream->rt.sampleRate;
  notification.output_channels = stream->rt.outputChannels;
  notification.input_channels = stream->rt.inputChannels;
  if (!stream->realtimeQueue.push(pending)) {
    bump(stream->counters.dropped, uint64_t{1});
    return;
  }
  wakeNotifier(stream->session);
}

void notifierLoop(AudIoSession* session) {
  while (true) {
    session->wake.wait();
    if (session->stopNotifier.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(session->listenerMutex);
    if (session->listener != nullptr) {
      session->listener(session->listenerUser);
    }
  }
}

// ............................................................................
// The realtime callback

// Silences the first `frames` frames of every channel of a bus.
void clearBus(const std::vector<float*>& channels,
              uint32_t frames) AUD_NONBLOCKING {
  for (float* channel : channels) {
    std::memset(channel, 0, sizeof(float) * frames);
  }
}

// Copies `frames` interleaved frames into the planes of `channels`.
void deinterleave(const float* interleaved, const std::vector<float*>& channels,
                  uint32_t frames) AUD_NONBLOCKING {
  const size_t stride = channels.size();
  for (size_t c = 0; c < stride; ++c) {
    float* plane = channels[c];
    for (uint32_t f = 0; f < frames; ++f) plane[f] = interleaved[f * stride + c];
  }
}

// Copies `frames` frames of the planes of `channels` into interleaved
// memory.
void interleave(const std::vector<float*>& channels, float* interleaved,
                uint32_t frames) AUD_NONBLOCKING {
  const size_t stride = channels.size();
  for (size_t c = 0; c < stride; ++c) {
    const float* plane = channels[c];
    for (uint32_t f = 0; f < frames; ++f) interleaved[f * stride + c] = plane[f];
  }
}

// Silences the interleaved output of a callback.
void silence(float* output, uint32_t frames,
             uint32_t channels) AUD_NONBLOCKING {
  if (output != nullptr && channels > 0) {
    std::memset(output, 0, sizeof(float) * frames * channels);
  }
}

// Counts a callback and reports a late one.
void countCallback(AudIoStream* stream, uint32_t frames,
                   int64_t callbackNs) AUD_NONBLOCKING {
  Realtime& rt = stream->rt;
  Counters& counters = stream->counters;
  if (stream->resetPeriod.load(std::memory_order_relaxed) &&
      stream->resetPeriod.exchange(false, std::memory_order_acq_rel)) {
    rt.lastCallbackNs = 0;
  }
  if (rt.lastCallbackNs != 0 && rt.sampleRate > 0) {
    const int64_t period = callbackNs - rt.lastCallbackNs;
    lower(counters.periodMin, period);
    raise(counters.periodMax, period);
    bump(counters.periodSum, period);
    bump(counters.periodCount, uint64_t{1});
    const double due =
        static_cast<double>(rt.lastCallbackFrames) * 1e9 / rt.sampleRate;
    if (static_cast<double>(period) > kLateFactor * due) {
      bump(counters.late, uint64_t{1});
    }
  }
  rt.lastCallbackNs = callbackNs;
  rt.lastCallbackFrames = frames;
  bump(counters.callbacks, uint64_t{1});
  bump(counters.frames, uint64_t{frames});
  lower(counters.callbackFramesMin, frames);
  raise(counters.callbackFramesMax, frames);
}

// The first callback after a start: the sample position runs on by the
// frames the device was away, at least one, and the control thread hears
// that audio flows again.
void handleRestart(AudIoStream* stream, int64_t hostNs,
                   int64_t callbackNs) AUD_NONBLOCKING {
  Realtime& rt = stream->rt;
  rt.restarted = false;
  if (rt.position > 0 || rt.nextHostNs != 0) {
    int64_t jump = 1;
    if (rt.nextHostNs != 0 && hostNs != 0) {
      const double gap = static_cast<double>(hostNs - rt.nextHostNs);
      jump = std::max<int64_t>(
          1, static_cast<int64_t>(std::llround(gap * rt.sampleRate / 1e9)));
    }
    rt.position += jump;
  }
  rt.nextHostNs = 0;
  rt.hasJitter = false;
  int64_t recovery = 0;
  if (rt.causeNs != 0) {
    recovery = callbackNs - rt.causeNs;
    stream->counters.recoveryLast.store(recovery, std::memory_order_relaxed);
    raise(stream->counters.recoveryMax, recovery);
    rt.causeNs = 0;
  }
  postRealtime(stream, AUD_IO_NOTIFY_STARTED, AUD_OK, recovery, callbackNs);
}

// Follows how far the host times deviate from the sample clock.
void trackJitter(AudIoStream* stream, int64_t hostNs,
                 uint32_t frames) AUD_NONBLOCKING {
  Realtime& rt = stream->rt;
  if (hostNs == 0) {
    rt.nextHostNs = 0;
    return;
  }
  if (rt.nextHostNs != 0) {
    const double deviation =
        std::fabs(static_cast<double>(hostNs - rt.nextHostNs));
    rt.jitterNs = rt.hasJitter
                      ? rt.jitterNs + (deviation - rt.jitterNs) * kJitterWeight
                      : deviation;
    rt.hasJitter = true;
    raise(stream->counters.jitterMax, static_cast<int64_t>(deviation));
  }
  rt.nextHostNs =
      hostNs + static_cast<int64_t>(std::llround(static_cast<double>(frames) *
                                                 1e9 / rt.sampleRate));
}

}  // namespace

void streamProcess(AudIoStream* stream, const float* input, float* output,
                   uint32_t frames, const DeviceTime& time) AUD_NONBLOCKING {
  const int64_t entered = nowNs();
  Realtime& rt = stream->rt;
  Counters& counters = stream->counters;
  const int64_t callbackNs = time.callbackNs != 0 ? time.callbackNs : entered;
  // The time a device was stopped or away is no period between callbacks.
  if (rt.restarted) rt.lastCallbackNs = 0;
  countCallback(stream, frames, callbackNs);

  // The host time of the block: the output time, for an input-only stream
  // the capture time.
  int64_t hostNs = rt.outputChannels > 0 ? time.outputNs : time.inputNs;
  const uint32_t source = hostNs != 0 ? time.source : AUD_TIME_SOURCE_NONE;
  if (source == AUD_TIME_SOURCE_NONE) hostNs = 0;
  if (rt.restarted) handleRestart(stream, hostNs, callbackNs);
  trackJitter(stream, hostNs, frames);

  AudStreamTime streamTime{};
  streamTime.struct_size = sizeof(AudStreamTime);
  streamTime.sample_rate = rt.sampleRate;
  streamTime.host_time_source = source;
  streamTime.host_time_accuracy_ns =
      rt.hasJitter ? static_cast<int64_t>(std::llround(rt.jitterNs)) : 0;
  streamTime.output_latency_frames = time.outputLatencyFrames;
  streamTime.input_latency_frames = time.inputLatencyFrames;

  // After a change of the format the renderer waits for its prepare.
  if (stream->acknowledged.load(std::memory_order_acquire) !=
      stream->generation.load(std::memory_order_acquire)) {
    silence(output, frames, rt.outputChannels);
    bump(counters.held, uint64_t{1});
    streamTime.frames = frames;
    streamTime.sample_position = rt.position;
    streamTime.host_time_ns = hostNs;
    stream->lastTime.store(streamTime);
    rt.position += frames;
  } else {
    const uint32_t numInputBuses = rt.inputChannels > 0 ? 1 : 0;
    const uint32_t numOutputBuses = rt.outputChannels > 0 ? 1 : 0;
    uint32_t done = 0;
    while (done < frames) {
      const uint32_t block = std::min(rt.maxFrames, frames - done);
      if (numInputBuses > 0 && input != nullptr) {
        deinterleave(input + size_t{done} * rt.inputChannels,
                     rt.inputPointers, block);
      } else if (numInputBuses > 0) {
        clearBus(rt.inputPointers, block);
      }
      clearBus(rt.outputPointers, block);
      streamTime.frames = block;
      streamTime.sample_position = rt.position;
      streamTime.host_time_ns =
          hostNs != 0
              ? hostNs + static_cast<int64_t>(std::llround(
                             static_cast<double>(done) * 1e9 / rt.sampleRate))
              : 0;
      AudRenderRequest request{};
      request.struct_size = sizeof(AudRenderRequest);
      request.frames = block;
      request.num_input_buses = numInputBuses;
      request.num_output_buses = numOutputBuses;
      request.inputs = numInputBuses > 0 ? &rt.inputBus : nullptr;
      request.outputs = numOutputBuses > 0 ? &rt.outputBus : nullptr;
      request.time = &streamTime;
      const int32_t result = rt.render(rt.user, &request);
      bump(counters.renders, uint64_t{1});
      if (result < 0) {
        bump(counters.renderErrors, uint64_t{1});
        clearBus(rt.outputPointers, block);
        if (!rt.renderFailing) {
          rt.renderFailing = true;
          postRealtime(stream, AUD_IO_NOTIFY_RENDER_ERROR, result, 0,
                       callbackNs);
        }
      } else {
        rt.renderFailing = false;
      }
      if (output != nullptr && numOutputBuses > 0) {
        interleave(rt.outputPointers,
                   output + size_t{done} * rt.outputChannels, block);
      }
      stream->lastTime.store(streamTime);
      rt.position += block;
      done += block;
    }
  }
  const int64_t elapsed = nowNs() - entered;
  raise(counters.callbackMax, elapsed);
  bump(counters.callbackSum, elapsed);
}

namespace {

// ............................................................................
// Devices

// Sizes the planar buffers for the format; no callback runs.
void prepareRealtime(AudIoStream* stream, const DeviceFormat& format) {
  Realtime& rt = stream->rt;
  rt.sampleRate = format.sampleRate;
  rt.outputChannels =
      (stream->direction & AUD_IO_OUTPUT) != 0 ? format.outputChannels : 0;
  rt.inputChannels =
      (stream->direction & AUD_IO_INPUT) != 0 ? format.inputChannels : 0;
  rt.outputPlanes.assign(size_t{rt.outputChannels} * rt.maxFrames, 0.0f);
  rt.inputPlanes.assign(size_t{rt.inputChannels} * rt.maxFrames, 0.0f);
  rt.outputPointers.resize(rt.outputChannels);
  rt.inputPointers.resize(rt.inputChannels);
  for (uint32_t c = 0; c < rt.outputChannels; ++c) {
    rt.outputPointers[c] = rt.outputPlanes.data() + size_t{c} * rt.maxFrames;
  }
  for (uint32_t c = 0; c < rt.inputChannels; ++c) {
    rt.inputPointers[c] = rt.inputPlanes.data() + size_t{c} * rt.maxFrames;
  }
  rt.outputBus = {sizeof(AudAudioBus), rt.outputChannels,
                  rt.outputPointers.data()};
  rt.inputBus = {sizeof(AudAudioBus), rt.inputChannels,
                 rt.inputPointers.data()};
}

// Opens the device of the stream; the control lock is held and no device is
// open. With `fallBack` a requested device that is gone gives way to the
// default device. Sets `changed` when the rate or the channels differ from
// the device before.
int32_t openDevice(AudIoStream* stream, bool fallBack, bool* changed) {
  Backend& backend = *stream->session->backend;
  int32_t result = AUD_OK;
  std::unique_ptr<Device> device =
      backend.open(stream->request, stream, &result);
  if (device == nullptr && result == AUD_IO_ERROR_NO_DEVICE && fallBack &&
      (!stream->request.outputId.empty() ||
       !stream->request.inputId.empty())) {
    stream->request.outputId.clear();
    stream->request.inputId.clear();
    device = backend.open(stream->request, stream, &result);
  }
  if (device == nullptr) return result < 0 ? result : AUD_IO_ERROR_DEVICE;
  const DeviceFormat& format = device->format();
  const bool differs =
      stream->opened && (format.sampleRate != stream->format.sampleRate ||
                         format.outputChannels != stream->format.outputChannels ||
                         format.inputChannels != stream->format.inputChannels);
  if (changed != nullptr) *changed = differs;
  prepareRealtime(stream, format);
  stream->format = format;
  stream->device = std::move(device);
  stream->opened = true;
  if (differs) {
    const uint32_t generation =
        stream->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    // A render function that follows the format on its own is not held.
    if ((stream->request.flags & AUD_IO_STREAM_FOLLOW_FORMAT) != 0) {
      stream->acknowledged.store(generation, std::memory_order_release);
    }
  }
  return AUD_OK;
}

// Closes the device of the stream; the control lock is held.
void closeDevice(AudIoStream* stream) {
  if (stream->device == nullptr) return;
  stream->xrunBase += stream->device->xruns();
  stream->device.reset();
}

// Starts the device; the control lock is held.
int32_t startDevice(AudIoStream* stream, int64_t causeNs) {
  const int32_t active = stream->session->backend->activate();
  if (active < 0) return active;
  stream->rt.restarted = true;
  stream->rt.causeNs = causeNs;
  const int32_t result = stream->device->start();
  if (result < 0) return result;
  stream->state.store(AUD_IO_STATE_RUNNING, std::memory_order_release);
  return AUD_OK;
}

// Waits before the next attempt of a retry, longer with every attempt,
// unless the stream closes; returns false when it closes. The control lock
// is released meanwhile.
bool backoff(AudIoStream* stream, std::unique_lock<std::mutex>& control,
             size_t& attempt) {
  const size_t last = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]) - 1;
  const int64_t ms = kBackoffMs[std::min(attempt, last)];
  attempt += 1;
  control.unlock();
  {
    std::unique_lock<std::mutex> lock(stream->workerMutex);
    stream->workerWake.wait_for(lock, std::chrono::milliseconds(ms), [&] {
      return stream->closing.load(std::memory_order_acquire);
    });
  }
  control.lock();
  return !stream->closing.load(std::memory_order_acquire);
}

// Opens the device again and starts it when the client wants it running,
// with backoff between the attempts; the control lock is held. `causeNs` is
// when the device was lost.
void reopen(AudIoStream* stream, std::unique_lock<std::mutex>& control,
            int64_t causeNs) {
  const int64_t giveUp = nowNs() + stream->session->giveUpNs;
  size_t attempt = 0;
  // A format that changed with an open whose start failed stays changed.
  bool formatChanged = false;
  const auto recovered = [&] {
    stream->counters.recoveries.fetch_add(1, std::memory_order_relaxed);
    AudIoNotification notification = streamNotification(
        stream,
        formatChanged ? AUD_IO_NOTIFY_FORMAT_CHANGED : AUD_IO_NOTIFY_RECOVERED,
        AUD_IO_REASON_NONE);
    notification.value = nowNs() - causeNs;
    post(stream->session, notification);
  };
  while (true) {
    bool changed = false;
    int32_t result = openDevice(stream, true, &changed);
    if (result == AUD_OK) {
      formatChanged = formatChanged || changed;
      if (!stream->wantRunning.load(std::memory_order_acquire)) {
        stream->state.store(AUD_IO_STATE_STOPPED, std::memory_order_release);
        recovered();
        return;
      }
      result = startDevice(stream, causeNs);
      if (result == AUD_OK) {
        recovered();
        return;
      }
      closeDevice(stream);
    }
    if (nowNs() >= giveUp) {
      stream->state.store(AUD_IO_STATE_FAILED, std::memory_order_release);
      AudIoNotification notification =
          streamNotification(stream, AUD_IO_NOTIFY_FAILED, AUD_IO_REASON_NONE);
      notification.code = result;
      post(stream->session, notification);
      return;
    }
    if (!backoff(stream, control, attempt)) return;
  }
}

// ............................................................................
// The worker

void handleRecovery(AudIoStream* stream, uint32_t reason, int64_t causeNs) {
  std::unique_lock<std::mutex> control(stream->control);
  if (reason != AUD_IO_REASON_REQUEST) {
    stream->counters.disconnects.fetch_add(1, std::memory_order_relaxed);
    post(stream->session,
         streamNotification(stream, AUD_IO_NOTIFY_DISCONNECTED, reason));
  }
  closeDevice(stream);
  if (stream->interrupted) return;  // reopened when the interruption ends
  stream->state.store(AUD_IO_STATE_RECOVERING, std::memory_order_release);
  reopen(stream, control, causeNs);
}

void handleInterruptBegin(AudIoStream* stream, uint32_t reason) {
  std::lock_guard<std::mutex> control(stream->control);
  if (stream->interrupted) return;
  stream->interrupted = true;
  stream->counters.interruptions.fetch_add(1, std::memory_order_relaxed);
  if (stream->state.load(std::memory_order_acquire) == AUD_IO_STATE_RUNNING) {
    stream->device->stop();
  }
  if (stream->wantRunning.load(std::memory_order_acquire)) {
    stream->state.store(AUD_IO_STATE_INTERRUPTED, std::memory_order_release);
  }
  post(stream->session,
       streamNotification(stream, AUD_IO_NOTIFY_INTERRUPTED, reason));
}

void handleInterruptEnd(AudIoStream* stream) {
  std::unique_lock<std::mutex> control(stream->control);
  if (!stream->interrupted) return;
  stream->interrupted = false;
  if (!stream->wantRunning.load(std::memory_order_acquire)) {
    if (stream->state.load(std::memory_order_acquire) ==
        AUD_IO_STATE_INTERRUPTED) {
      stream->state.store(AUD_IO_STATE_STOPPED, std::memory_order_release);
    }
    post(stream->session, streamNotification(stream, AUD_IO_NOTIFY_RESUMED,
                                             AUD_IO_REASON_NONE));
    return;
  }
  const int64_t causeNs = nowNs();
  stream->state.store(AUD_IO_STATE_RECOVERING, std::memory_order_release);
  if (stream->device != nullptr) {
    // The system may hold the audio a little longer than it says.
    const int64_t giveUp = causeNs + stream->session->giveUpNs;
    size_t attempt = 0;
    while (true) {
      const int32_t result = startDevice(stream, causeNs);
      if (result == AUD_OK) break;
      if (result != AUD_IO_ERROR_INTERRUPTED || nowNs() >= giveUp) {
        closeDevice(stream);
        break;
      }
      if (!backoff(stream, control, attempt)) return;
    }
  }
  if (stream->device == nullptr) reopen(stream, control, causeNs);
  post(stream->session, streamNotification(stream, AUD_IO_NOTIFY_RESUMED,
                                           AUD_IO_REASON_NONE));
}

void handleRouteChanged(AudIoStream* stream, uint32_t reason) {
  std::lock_guard<std::mutex> control(stream->control);
  post(stream->session,
       streamNotification(stream, AUD_IO_NOTIFY_ROUTE_CHANGED, reason));
}

void workerLoop(AudIoStream* stream) {
  std::unique_lock<std::mutex> lock(stream->workerMutex);
  while (true) {
    stream->workerWake.wait(lock, [&] {
      return stream->closing.load(std::memory_order_acquire) ||
             stream->recoverPending || stream->interruptBeginPending ||
             stream->interruptEndPending || stream->routePending;
    });
    if (stream->closing.load(std::memory_order_acquire)) return;
    const bool recover = stream->recoverPending;
    const uint32_t reason = stream->recoverReason;
    const int64_t causeNs = stream->recoverCauseNs;
    const bool begin = stream->interruptBeginPending;
    const bool end = stream->interruptEndPending;
    const uint32_t interruptReason = stream->interruptReason;
    const bool route = stream->routePending;
    const uint32_t routeReason = stream->routeReason;
    stream->recoverPending = false;
    stream->interruptBeginPending = false;
    stream->interruptEndPending = false;
    stream->routePending = false;
    lock.unlock();
    if (begin) handleInterruptBegin(stream, interruptReason);
    if (recover) handleRecovery(stream, reason, causeNs);
    if (route) handleRouteChanged(stream, routeReason);
    if (end) handleInterruptEnd(stream);
    lock.lock();
  }
}

void requestRecovery(AudIoStream* stream, uint32_t reason) {
  {
    std::lock_guard<std::mutex> lock(stream->workerMutex);
    if (!stream->recoverPending) {
      stream->recoverCauseNs = nowNs();
      stream->recoverReason = reason;
    }
    stream->recoverPending = true;
  }
  stream->workerWake.notify_all();
}

void requestInterruption(AudIoStream* stream, bool began, uint32_t reason) {
  {
    std::lock_guard<std::mutex> lock(stream->workerMutex);
    if (began) {
      stream->interruptBeginPending = true;
      stream->interruptReason = reason;
    } else {
      stream->interruptEndPending = true;
    }
  }
  stream->workerWake.notify_all();
}

// Moves the notifications the audio thread of `stream` posted into the
// session's queue; the control thread is the only consumer.
void drainRealtime(AudIoStream* stream, std::vector<Pending>& out) {
  Pending pending;
  while (stream->realtimeQueue.pop(pending)) out.push_back(pending);
}

bool validConfig(const AudIoStreamConfig* config) {
  if (config == nullptr || config->struct_size < sizeof(AudIoStreamConfig) ||
      config->render == nullptr) {
    return false;
  }
  if (config->direction != AUD_IO_OUTPUT && config->direction != AUD_IO_INPUT &&
      config->direction != AUD_IO_DUPLEX) {
    return false;
  }
  if (config->output_channels > AUD_IO_MAX_CHANNELS ||
      config->input_channels > AUD_IO_MAX_CHANNELS ||
      config->max_frames > kMaxMaxFrames ||
      config->buffer_frames > kMaxMaxFrames ||
      config->performance_mode > AUD_IO_PERFORMANCE_POWER_SAVING) {
    return false;
  }
  return config->sample_rate == 0 ||
         (config->sample_rate >= 8000 && config->sample_rate <= 384000);
}

}  // namespace

// ............................................................................
// What the backends call

void streamDeviceLost(AudIoStream* stream, uint32_t reason) {
  requestRecovery(stream, reason);
}

void streamRouteChanged(AudIoStream* stream, uint32_t reason) {
  {
    std::lock_guard<std::mutex> lock(stream->workerMutex);
    stream->routePending = true;
    stream->routeReason = reason;
  }
  stream->workerWake.notify_all();
}

void sessionInterruption(AudIoSession* session, bool began, uint32_t reason) {
  std::lock_guard<std::mutex> lock(session->streamsMutex);
  for (AudIoStream* stream : session->streams) {
    requestInterruption(stream, began, reason);
  }
}

void sessionDevicesChanged(AudIoSession* session) {
  post(session, makeNotification(AUD_IO_NOTIFY_DEVICES_CHANGED, 0,
                                 AUD_IO_REASON_NONE));
}

void sessionPermissionChanged(AudIoSession* session, int32_t permission) {
  AudIoNotification notification =
      makeNotification(AUD_IO_NOTIFY_PERMISSION, 0, AUD_IO_REASON_NONE);
  notification.code = permission;
  post(session, notification);
}

void sessionAllDevicesLost(AudIoSession* session, uint32_t reason) {
  std::lock_guard<std::mutex> lock(session->streamsMutex);
  for (AudIoStream* stream : session->streams) {
    requestRecovery(stream, reason);
  }
}

void copyString(char* out, size_t capacity, const std::string& value) {
  if (out == nullptr || capacity == 0) return;
  const size_t length = std::min(value.size(), capacity - 1);
  std::memcpy(out, value.data(), length);
  out[length] = '\0';
}

}  // namespace aud_io

using aud_io::Pending;

// ############################################################################
// Session

AUD_EXPORT AudIoSession* aud_io_session_create(
    const AudIoSessionConfig* config) {
  if (config == nullptr || config->struct_size < sizeof(AudIoSessionConfig) ||
      config->backend > AUD_IO_BACKEND_NULL ||
      (config->directions & ~uint32_t{AUD_IO_DUPLEX}) != 0) {
    return nullptr;
  }
  auto* session = new (std::nothrow) AudIoSession();
  if (session == nullptr) return nullptr;
  session->config = *config;
  session->config.struct_size = sizeof(AudIoSessionConfig);
  if (session->config.directions == 0) {
    session->config.directions = AUD_IO_OUTPUT;
  }
  if (session->config.notification_capacity > 0) {
    session->capacity = session->config.notification_capacity;
  }
  const uint32_t timeoutMs = session->config.recovery_timeout_ms > 0
                                 ? session->config.recovery_timeout_ms
                                 : aud_io::kDefaultRecoveryTimeoutMs;
  session->giveUpNs = int64_t{timeoutMs} * 1'000'000;
  session->backend =
      config->backend == AUD_IO_BACKEND_NULL
          ? aud_io::createNullBackend(session, session->config)
          : aud_io::createPlatformBackend(session, session->config);
  if (session->backend == nullptr) {
    delete session;
    return nullptr;
  }
  session->notifier = std::thread(aud_io::notifierLoop, session);
  return session;
}

AUD_EXPORT void aud_io_session_destroy(AudIoSession* session) {
  if (session == nullptr) return;
  while (true) {
    AudIoStream* stream = nullptr;
    {
      std::lock_guard<std::mutex> lock(session->streamsMutex);
      if (!session->streams.empty()) stream = session->streams.back();
    }
    if (stream == nullptr) break;
    aud_io_stream_close(stream);
  }
  session->stopNotifier.store(true, std::memory_order_release);
  session->wake.post();
  session->notifier.join();
  session->backend.reset();
  delete session;
}

AUD_EXPORT const char* aud_io_session_backend_name(AudIoSession* session) {
  return session == nullptr ? "" : session->backend->name();
}

AUD_EXPORT int32_t aud_io_session_devices(AudIoSession* session,
                                          AudIoDevice* out,
                                          uint32_t capacity) {
  if (session == nullptr || (out == nullptr && capacity > 0)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  std::vector<AudIoDevice> devices;
  const int32_t result = session->backend->devices(devices);
  if (result < 0) return result;
  for (size_t i = 0; i < devices.size() && i < capacity; ++i) {
    out[i] = devices[i];
  }
  return static_cast<int32_t>(devices.size());
}

AUD_EXPORT int32_t aud_io_session_permission(AudIoSession* session) {
  if (session == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  return session->backend->permission();
}

AUD_EXPORT int32_t aud_io_session_request_permission(AudIoSession* session) {
  if (session == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  return session->backend->requestPermission();
}

AUD_EXPORT int32_t aud_io_session_set_listener(AudIoSession* session,
                                               AudIoListener listener,
                                               void* user) {
  if (session == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  std::lock_guard<std::mutex> lock(session->listenerMutex);
  session->listener = listener;
  session->listenerUser = user;
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_session_take_notifications(AudIoSession* session,
                                                     AudIoNotification* out,
                                                     uint32_t capacity) {
  if (session == nullptr || (out == nullptr && capacity > 0)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  session->wakePending.store(false, std::memory_order_seq_cst);
  std::vector<Pending> taken;
  {
    std::lock_guard<std::mutex> lock(session->streamsMutex);
    for (AudIoStream* stream : session->streams) {
      aud_io::drainRealtime(stream, taken);
    }
  }
  std::lock_guard<std::mutex> lock(session->queueMutex);
  if (!taken.empty()) {
    session->queue.insert(session->queue.end(), taken.begin(), taken.end());
  }
  std::stable_sort(session->queue.begin(), session->queue.end(),
                   [](const Pending& a, const Pending& b) {
                     return a.sequence < b.sequence;
                   });
  uint32_t count = 0;
  while (count < capacity && !session->queue.empty()) {
    out[count] = session->queue.front().notification;
    session->queue.pop_front();
    count += 1;
  }
  // More than one take can hold; wake the listener again.
  if (!session->queue.empty()) aud_io::wakeNotifier(session);
  return static_cast<int32_t>(count);
}

AUD_EXPORT int32_t aud_io_session_interrupt(AudIoSession* session,
                                            uint32_t reason) {
  if (session == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  aud_io::sessionInterruption(session, true, reason);
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_session_resume(AudIoSession* session) {
  if (session == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  aud_io::sessionInterruption(session, false, AUD_IO_REASON_NONE);
  return AUD_OK;
}

// ############################################################################
// Streams

AUD_EXPORT AudIoStream* aud_io_stream_open(AudIoSession* session,
                                           const AudIoStreamConfig* config,
                                           int32_t* result) {
  int32_t ignored = AUD_OK;
  int32_t& outcome = result != nullptr ? *result : ignored;
  if (session == nullptr || !aud_io::validConfig(config)) {
    outcome = AUD_ERROR_INVALID_ARGUMENT;
    return nullptr;
  }
  auto* stream = new (std::nothrow) AudIoStream(session);
  if (stream == nullptr) {
    outcome = AUD_ERROR_OUT_OF_MEMORY;
    return nullptr;
  }
  stream->direction = config->direction;
  aud_io::DeviceRequest& request = stream->request;
  request.direction = config->direction;
  if (config->output_device_id != nullptr) {
    request.outputId = config->output_device_id;
  }
  if (config->input_device_id != nullptr) {
    request.inputId = config->input_device_id;
  }
  request.outputChannels = config->output_channels;
  request.inputChannels = config->input_channels;
  request.sampleRate = config->sample_rate;
  request.bufferFrames = config->buffer_frames;
  request.maxFrames =
      config->max_frames > 0 ? config->max_frames : aud_io::kDefaultMaxFrames;
  request.performanceMode = config->performance_mode;
  request.flags = config->flags;
  stream->rt.render = config->render;
  stream->rt.user = config->render_user;
  stream->rt.maxFrames = request.maxFrames;
  {
    std::lock_guard<std::mutex> control(stream->control);
    const int32_t opened = aud_io::openDevice(stream, false, nullptr);
    if (opened < 0) {
      outcome = opened;
      delete stream;
      return nullptr;
    }
  }
  {
    std::lock_guard<std::mutex> lock(session->streamsMutex);
    stream->id = session->nextStreamId++;
    session->streams.push_back(stream);
  }
  stream->worker = std::thread(aud_io::workerLoop, stream);
  outcome = AUD_OK;
  return stream;
}

AUD_EXPORT void aud_io_stream_close(AudIoStream* stream) {
  if (stream == nullptr) return;
  AudIoSession* session = stream->session;
  {
    std::lock_guard<std::mutex> lock(session->streamsMutex);
    auto& streams = session->streams;
    streams.erase(std::remove(streams.begin(), streams.end(), stream),
                  streams.end());
  }
  {
    std::lock_guard<std::mutex> lock(stream->workerMutex);
    stream->closing.store(true, std::memory_order_release);
  }
  stream->workerWake.notify_all();
  stream->worker.join();
  {
    std::lock_guard<std::mutex> control(stream->control);
    if (stream->device != nullptr &&
        stream->state.load(std::memory_order_acquire) ==
            AUD_IO_STATE_RUNNING) {
      stream->device->stop();
    }
    aud_io::closeDevice(stream);
  }
  // What the audio thread posted last still reaches the control thread.
  std::vector<Pending> taken;
  aud_io::drainRealtime(stream, taken);
  if (!taken.empty()) {
    std::lock_guard<std::mutex> lock(session->queueMutex);
    session->queue.insert(session->queue.end(), taken.begin(), taken.end());
  }
  delete stream;
}

AUD_EXPORT int32_t aud_io_stream_start(AudIoStream* stream) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  std::lock_guard<std::mutex> control(stream->control);
  const int32_t state = stream->state.load(std::memory_order_acquire);
  if (state == AUD_IO_STATE_RUNNING) return AUD_ERROR_STATE;
  stream->wantRunning.store(true, std::memory_order_release);
  if (state == AUD_IO_STATE_RECOVERING) return AUD_OK;  // the worker starts it
  if (stream->interrupted) {
    stream->state.store(AUD_IO_STATE_INTERRUPTED, std::memory_order_release);
    return AUD_OK;
  }
  if (stream->device == nullptr) {
    stream->state.store(AUD_IO_STATE_RECOVERING, std::memory_order_release);
    aud_io::requestRecovery(stream, AUD_IO_REASON_REQUEST);
    return AUD_OK;
  }
  const int32_t result = aud_io::startDevice(stream, 0);
  if (result < 0) stream->wantRunning.store(false, std::memory_order_release);
  return result;
}

AUD_EXPORT int32_t aud_io_stream_stop(AudIoStream* stream) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  // Before the lock, so that a recovery in progress does not start.
  const bool wanted =
      stream->wantRunning.exchange(false, std::memory_order_acq_rel);
  std::lock_guard<std::mutex> control(stream->control);
  const int32_t state = stream->state.load(std::memory_order_acquire);
  if (state == AUD_IO_STATE_RECOVERING) return AUD_OK;  // ends stopped
  if (!wanted && state == AUD_IO_STATE_STOPPED) return AUD_ERROR_STATE;
  if (state == AUD_IO_STATE_RUNNING) stream->device->stop();
  stream->state.store(AUD_IO_STATE_STOPPED, std::memory_order_release);
  aud_io::post(stream->session,
               aud_io::streamNotification(stream, AUD_IO_NOTIFY_STOPPED,
                                          AUD_IO_REASON_REQUEST));
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_stream_state(AudIoStream* stream) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  return stream->state.load(std::memory_order_acquire);
}

AUD_EXPORT int32_t aud_io_stream_id(AudIoStream* stream) {
  return stream == nullptr ? AUD_ERROR_INVALID_ARGUMENT : stream->id;
}

AUD_EXPORT int32_t aud_io_stream_format(AudIoStream* stream,
                                        AudIoStreamFormat* out) {
  if (stream == nullptr || out == nullptr ||
      out->struct_size < sizeof(AudIoStreamFormat)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> control(stream->control);
  const aud_io::DeviceFormat& format = stream->format;
  out->direction = stream->direction;
  out->sample_rate = format.sampleRate;
  out->output_channels = stream->rt.outputChannels;
  out->input_channels = stream->rt.inputChannels;
  out->max_frames = stream->rt.maxFrames;
  out->buffer_frames = format.bufferFrames;
  out->burst_frames = format.burstFrames;
  out->generation = stream->generation.load(std::memory_order_acquire);
  out->performance_mode = format.performanceMode;
  out->exclusive = format.exclusive ? 1 : 0;
  out->time_source = format.timeSource;
  out->reserved = 0;
  aud_io::copyString(out->output_device_id, sizeof(out->output_device_id),
                     format.outputId);
  aud_io::copyString(out->input_device_id, sizeof(out->input_device_id),
                     format.inputId);
  aud_io::copyString(out->backend, sizeof(out->backend), format.backend);
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_stream_acknowledge(AudIoStream* stream,
                                             uint32_t generation) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  if (generation != stream->generation.load(std::memory_order_acquire)) {
    return AUD_ERROR_STATE;
  }
  stream->acknowledged.store(generation, std::memory_order_release);
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_stream_counters(AudIoStream* stream,
                                          AudIoCounters* out) {
  if (stream == nullptr || out == nullptr ||
      out->struct_size < sizeof(AudIoCounters)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  const aud_io::Counters& c = stream->counters;
  const auto relaxed = std::memory_order_relaxed;
  {
    // A recovery holds the device for a while; the counters do not wait.
    std::unique_lock<std::mutex> control(stream->control, std::try_to_lock);
    if (control.owns_lock()) {
      const uint64_t xruns =
          stream->xrunBase +
          (stream->device != nullptr ? stream->device->xruns() : 0);
      stream->xrunsSeen.store(
          xruns >= stream->xrunReset ? xruns - stream->xrunReset : 0,
          relaxed);
    }
  }
  out->state = static_cast<uint32_t>(stream->state.load(relaxed));
  out->callbacks = c.callbacks.load(relaxed);
  out->frames = c.frames.load(relaxed);
  out->renders = c.renders.load(relaxed);
  out->callback_frames_min =
      out->callbacks == 0 ? 0 : c.callbackFramesMin.load(relaxed);
  out->callback_frames_max = c.callbackFramesMax.load(relaxed);
  out->period_count = c.periodCount.load(relaxed);
  out->period_min_ns = out->period_count == 0 ? 0 : c.periodMin.load(relaxed);
  out->period_max_ns = c.periodMax.load(relaxed);
  out->period_sum_ns = c.periodSum.load(relaxed);
  out->late_callbacks = c.late.load(relaxed);
  out->xruns = stream->xrunsSeen.load(relaxed);
  out->disconnects = c.disconnects.load(relaxed);
  out->recoveries = c.recoveries.load(relaxed);
  out->interruptions = c.interruptions.load(relaxed);
  out->held_blocks = c.held.load(relaxed);
  out->render_errors = c.renderErrors.load(relaxed);
  const uint64_t sessionDropped = stream->session->dropped.load(relaxed);
  const uint64_t sessionReset = stream->sessionDroppedReset.load(relaxed);
  out->notifications_dropped =
      c.dropped.load(relaxed) +
      (sessionDropped >= sessionReset ? sessionDropped - sessionReset : 0);
  out->callback_time_max_ns = c.callbackMax.load(relaxed);
  out->callback_time_sum_ns = c.callbackSum.load(relaxed);
  out->recovery_time_max_ns = c.recoveryMax.load(relaxed);
  out->recovery_time_last_ns = c.recoveryLast.load(relaxed);
  out->host_time_jitter_max_ns = c.jitterMax.load(relaxed);
  out->last_time = stream->lastTime.load();
  return AUD_OK;
}

AUD_EXPORT int32_t aud_io_stream_reset_counters(AudIoStream* stream) {
  if (stream == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  std::lock_guard<std::mutex> control(stream->control);
  stream->counters.reset();
  stream->sessionDroppedReset.store(
      stream->session->dropped.load(std::memory_order_relaxed),
      std::memory_order_relaxed);
  stream->xrunReset = stream->xrunBase + (stream->device != nullptr
                                              ? stream->device->xruns()
                                              : 0);
  stream->xrunsSeen.store(0, std::memory_order_relaxed);
  stream->resetPeriod.store(true, std::memory_order_release);
  return AUD_OK;
}

// ############################################################################
// Null backend

AUD_EXPORT int32_t aud_io_null_inject(AudIoSession* session, uint32_t fault,
                                      int64_t value) {
  if (session == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  return session->backend->inject(fault, value);
}

AUD_EXPORT int32_t aud_io_null_set_block_sizes(AudIoSession* session,
                                               const uint32_t* sizes,
                                               uint32_t count) {
  if (session == nullptr || (sizes == nullptr && count > 0)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  return session->backend->setBlockSizes(sizes, count);
}

AUD_EXPORT int32_t aud_io_null_process(AudIoStream* stream,
                                       const float* input, float* output,
                                       uint32_t frames, int64_t host_time_ns) {
  if (stream == nullptr || frames == 0) return AUD_ERROR_INVALID_ARGUMENT;
  return stream->session->backend->process(stream, input, output, frames,
                                           host_time_ns);
}
