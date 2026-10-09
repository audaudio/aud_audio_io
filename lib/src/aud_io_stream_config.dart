// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

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
}
