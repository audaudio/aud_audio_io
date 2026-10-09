// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';

import 'aud_io_direction.dart';
import 'aud_io_performance_mode.dart';

// #############################################################################
/// What a stream got from its device. A notification with a new
/// generation means a new format.
class AudIoStreamFormat {
  /// Creates a format.
  const AudIoStreamFormat({
    required this.direction,
    required this.sampleRate,
    required this.outputChannels,
    required this.inputChannels,
    required this.maxFrames,
    required this.bufferFrames,
    required this.burstFrames,
    required this.generation,
    required this.performanceMode,
    required this.exclusive,
    required this.timeSource,
    required this.outputDeviceId,
    required this.inputDeviceId,
    required this.backend,
  });

  // ...........................................................................
  /// Output, input or duplex.
  final AudIoDirection direction;

  /// The rate the render function runs at.
  final double sampleRate;

  /// The output channels; 0 for an input stream.
  final int outputChannels;

  /// The input channels; 0 for an output stream.
  final int inputChannels;

  /// The largest block the render function receives.
  final int maxFrames;

  /// The device buffer.
  final int bufferFrames;

  /// The device's period; on Android the burst.
  final int burstFrames;

  /// Grows with every change of the rate or the channels.
  final int generation;

  /// The performance mode the device granted.
  final AudIoPerformanceMode performanceMode;

  /// Whether exclusive sharing was granted (Android MMAP).
  final bool exclusive;

  /// Where the host times of the stream come from.
  final AudTimeSource timeSource;

  /// The output device; empty for an input stream.
  final String outputDeviceId;

  /// The input device; empty for an output stream.
  final String inputDeviceId;

  /// The backend, e.g. `oboe/aaudio` or `null`.
  final String backend;

  /// The device buffer as a duration.
  Duration get bufferDuration => Duration(
    microseconds: sampleRate == 0
        ? 0
        : (bufferFrames * 1e6 / sampleRate).round(),
  );

  @override
  String toString() =>
      'AudIoStreamFormat($backend, ${sampleRate.toStringAsFixed(0)} Hz, '
      '$outputChannels out, $inputChannels in, buffer $bufferFrames, '
      'generation $generation)';
}
