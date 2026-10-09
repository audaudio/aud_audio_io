// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:aud_audio_core/aud_audio_core_ffi.dart';
import 'package:ffi/ffi.dart';

import 'aud_audio_io_bindings_generated.dart' as bindings;
import 'aud_io_counters.dart';
import 'aud_io_device.dart';
import 'aud_io_direction.dart';
import 'aud_io_native_string.dart';
import 'aud_io_notification.dart';
import 'aud_io_performance_mode.dart';
import 'aud_io_route.dart';
import 'aud_io_state.dart';
import 'aud_io_stream_config.dart';
import 'aud_io_stream_format.dart';

// The conversions between the platform-neutral types of the IO and the
// structs of its C API, apart from the types so that no dart:ffi reaches a
// web build (web-001).

// #############################################################################
/// Reads an [AudIoStreamFormat] from its native struct.
extension AudNativeIoStreamFormatToDart on bindings.AudIoStreamFormat {
  /// Reads a format of the C API.
  AudIoStreamFormat toDart() => AudIoStreamFormat(
    direction: AudIoDirection.fromCode(direction),
    sampleRate: sample_rate,
    outputChannels: output_channels,
    inputChannels: input_channels,
    maxFrames: max_frames,
    bufferFrames: buffer_frames,
    burstFrames: burst_frames,
    generation: generation,
    performanceMode: AudIoPerformanceMode.fromCode(performance_mode),
    exclusive: exclusive != 0,
    timeSource: AudTimeSource.fromCode(time_source),
    outputDeviceId: AudIoNativeString.read(
      output_device_id,
      bindings.AUD_IO_MAX_ID,
    ),
    inputDeviceId: AudIoNativeString.read(
      input_device_id,
      bindings.AUD_IO_MAX_ID,
    ),
    backend: AudIoNativeString.read(backend, 64),
  );
}

/// Reads an [AudIoNotification] from its native struct.
extension AudNativeIoNotificationToDart on bindings.AudIoNotification {
  /// Reads a notification of the C API.
  AudIoNotification toDart() => AudIoNotification(
    type: AudIoNotificationType.fromCode(type),
    streamId: stream,
    code: code,
    reason: AudIoReason.fromCode(reason),
    generation: generation,
    hostTimeNs: host_time_ns,
    value: value,
    sampleRate: sample_rate,
    outputChannels: output_channels,
    inputChannels: input_channels,
  );
}

/// Reads an [AudIoCounters] from its native struct.
extension AudNativeIoCountersToDart on bindings.AudIoCounters {
  /// Reads the counters of the C API.
  AudIoCounters toDart() => AudIoCounters(
    state: AudIoState.fromCode(state),
    callbacks: callbacks,
    frames: frames,
    renders: renders,
    callbackFramesMin: callback_frames_min,
    callbackFramesMax: callback_frames_max,
    periodMinNs: period_min_ns,
    periodMaxNs: period_max_ns,
    periodSumNs: period_sum_ns,
    periodCount: period_count,
    lateCallbacks: late_callbacks,
    xruns: xruns,
    disconnects: disconnects,
    recoveries: recoveries,
    interruptions: interruptions,
    heldBlocks: held_blocks,
    renderErrors: render_errors,
    notificationsDropped: notifications_dropped,
    callbackTimeMaxNs: callback_time_max_ns,
    callbackTimeSumNs: callback_time_sum_ns,
    recoveryTimeMaxNs: recovery_time_max_ns,
    recoveryTimeLastNs: recovery_time_last_ns,
    hostTimeJitterMaxNs: host_time_jitter_max_ns,
    lastTime: last_time.toDart(),
  );
}

/// Reads an [AudIoDevice] from its native struct.
extension AudNativeIoDeviceToDart on bindings.AudIoDevice {
  /// Reads a device of the C API.
  AudIoDevice toDart() => AudIoDevice(
    id: AudIoNativeString.read(id, bindings.AUD_IO_MAX_ID),
    name: AudIoNativeString.read(name, bindings.AUD_IO_MAX_NAME),
    directions: AudIoDirection.fromCode(directions),
    route: AudIoRoute.fromCode(route),
    isDefaultOutput: (flags & bindings.AUD_IO_DEVICE_DEFAULT_OUTPUT) != 0,
    isDefaultInput: (flags & bindings.AUD_IO_DEVICE_DEFAULT_INPUT) != 0,
    isActive: (flags & bindings.AUD_IO_DEVICE_ACTIVE) != 0,
    maxOutputChannels: max_output_channels,
    maxInputChannels: max_input_channels,
    sampleRates: List.unmodifiable([
      for (var i = 0; i < num_sample_rates; i++) sample_rates[i],
    ]),
  );
}

/// Writes an [AudIoStreamConfig] into the struct of the C API.
extension AudIoStreamConfigToNative on AudIoStreamConfig {
  /// Writes the configuration with [render] and [user] into a struct of the
  /// C API allocated in [arena].
  Pointer<bindings.AudIoStreamConfig> toNative(
    Arena arena, {
    required core.AudRenderFunction render,
    Pointer<Void>? user,
  }) {
    final native = arena<bindings.AudIoStreamConfig>();
    native.ref
      ..struct_size = sizeOf<bindings.AudIoStreamConfig>()
      ..direction = direction.code
      ..output_device_id = outputDeviceId == null
          ? nullptr
          : outputDeviceId!.toNativeUtf8(allocator: arena).cast()
      ..input_device_id = inputDeviceId == null
          ? nullptr
          : inputDeviceId!.toNativeUtf8(allocator: arena).cast()
      ..output_channels = outputChannels
      ..input_channels = inputChannels
      ..sample_rate = sampleRate
      ..buffer_frames = bufferFrames
      ..max_frames = maxFrames
      ..performance_mode = performanceMode.code
      ..flags =
          (exclusive ? bindings.AUD_IO_STREAM_EXCLUSIVE : 0) |
          (latencyTuner ? bindings.AUD_IO_STREAM_LATENCY_TUNER : 0) |
          (followFormat ? bindings.AUD_IO_STREAM_FOLLOW_FORMAT : 0)
      ..render = render
      ..render_user = user ?? nullptr;
    return native;
  }
}
