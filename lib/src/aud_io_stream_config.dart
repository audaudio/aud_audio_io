// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:ffi/ffi.dart';

import 'aud_audio_io_bindings_generated.dart' as bindings;
import 'aud_io_direction.dart';
import 'aud_io_performance_mode.dart';

// #############################################################################
/// What a stream asks for. A value left at 0 or null takes the default.
class AudIoStreamConfig {
  /// Creates a configuration.
  ///
  /// - [direction] output, input or duplex
  /// - [outputDeviceId] the output; null: the default output
  /// - [inputDeviceId] the input; null: the default input
  /// - [outputChannels] 0: the route's channels, at most 2
  /// - [inputChannels] 0: the route's channels, at most 2
  /// - [sampleRate] 0: the device's rate, followed across route changes; a
  ///   fixed rate is converted by the backend
  /// - [bufferFrames] the device buffer; 0: the low-latency default
  /// - [maxFrames] the largest block the render function receives; device
  ///   callbacks above it are split; 0 = 1024
  /// - [performanceMode] what the stream optimizes for
  /// - [exclusive] Android: ask for exclusive sharing, the MMAP path
  /// - [latencyTuner] Android: grow the output buffer from one burst until
  ///   the underruns stop
  /// - [followFormat] the render function follows a new rate or channel
  ///   count on its own, as those of this package do: the stream does not
  ///   hold it after a format change
  const AudIoStreamConfig({
    this.direction = AudIoDirection.output,
    this.outputDeviceId,
    this.inputDeviceId,
    this.outputChannels = 0,
    this.inputChannels = 0,
    this.sampleRate = 0,
    this.bufferFrames = 0,
    this.maxFrames = 0,
    this.performanceMode = AudIoPerformanceMode.lowLatency,
    this.exclusive = true,
    this.latencyTuner = true,
    this.followFormat = false,
  });

  // ...........................................................................
  /// Output, input or duplex.
  final AudIoDirection direction;

  /// The output; null: the default output.
  final String? outputDeviceId;

  /// The input; null: the default input.
  final String? inputDeviceId;

  /// 0: the route's channels, at most 2.
  final int outputChannels;

  /// 0: the route's channels, at most 2.
  final int inputChannels;

  /// 0: the device's rate, followed across route changes.
  final double sampleRate;

  /// The device buffer; 0: the low-latency default.
  final int bufferFrames;

  /// The largest block the render function receives; 0 = 1024.
  final int maxFrames;

  /// What the stream optimizes for.
  final AudIoPerformanceMode performanceMode;

  /// Android: ask for exclusive sharing.
  final bool exclusive;

  /// Android: let Oboe's latency tuner size the output buffer.
  final bool latencyTuner;

  /// The render function follows a new format on its own; no acknowledge.
  final bool followFormat;

  // ...........................................................................
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
