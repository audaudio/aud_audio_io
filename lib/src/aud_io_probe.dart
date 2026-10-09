// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:ffi/ffi.dart';

import 'aud_audio_io_bindings_generated.dart' as bindings;
import 'aud_io_exception.dart';

// #############################################################################
/// What a latency probe measured.
class AudIoProbeResult {
  /// Creates a result.
  const AudIoProbeResult({
    required this.clicks,
    required this.detections,
    required this.lastRoundTripFrames,
    required this.minRoundTripFrames,
    required this.maxRoundTripFrames,
    required this.sumRoundTripFrames,
    required this.reportedRoundTripFrames,
    required this.inputPeak,
  });

  /// Reads a result of the C API.
  factory AudIoProbeResult.fromNative(bindings.AudIoProbeResult native) =>
      AudIoProbeResult(
        clicks: native.clicks,
        detections: native.detections,
        lastRoundTripFrames: native.last_round_trip_frames,
        minRoundTripFrames: native.min_round_trip_frames,
        maxRoundTripFrames: native.max_round_trip_frames,
        sumRoundTripFrames: native.sum_round_trip_frames,
        reportedRoundTripFrames: native.reported_round_trip_frames,
        inputPeak: native.input_peak,
      );

  /// Clicks played.
  final int clicks;

  /// Clicks found again at the input.
  final int detections;

  /// The frames from the last click to its onset at the input.
  final int lastRoundTripFrames;

  /// The shortest round trip.
  final int minRoundTripFrames;

  /// The longest round trip.
  final int maxRoundTripFrames;

  /// The round trips, summed.
  final int sumRoundTripFrames;

  /// The output plus the input latency the stream reported for the last
  /// click found.
  final int reportedRoundTripFrames;

  /// The largest absolute input sample so far: whether the input hears
  /// anything, and how loud the clicks come back.
  final double inputPeak;

  /// The mean round trip.
  double get meanRoundTripFrames =>
      detections == 0 ? 0 : sumRoundTripFrames / detections;

  /// How far the reported latency misses the measured round trip.
  int get errorFrames => lastRoundTripFrames - reportedRoundTripFrames;
}

// #############################################################################
/// The latency probe: a render function for a duplex stream that plays a
/// click - a 5 ms burst of 2 kHz - every interval and finds it again at the
/// input through a loop from the output to the input, a cable or the air. It measures how far
/// the latencies a device reports miss the real round trip.
class AudIoProbe {
  /// Creates a probe.
  ///
  /// - [intervalFrames] the frames from one click to the next
  /// - [threshold] the input level of an onset; 0 = 0.1
  AudIoProbe({required int intervalFrames, double threshold = 0}) {
    _pointer = bindings.aud_io_probe_create(intervalFrames, threshold);
    if (_pointer == nullptr) {
      throw AudIoException(
        core.AUD_ERROR_INVALID_ARGUMENT,
        'A probe needs an interval and a threshold of at least 0, '
        'not $intervalFrames and $threshold.',
      );
    }
  }

  // ...........................................................................
  /// The render function of the probe; [pointer] is its user.
  static core.AudRenderFunction get render =>
      Native.addressOf<NativeFunction<core.AudRenderFunctionFunction>>(
        bindings.aud_io_probe_render,
      );

  /// The native probe, the user of [render].
  Pointer<Void> get pointer => _pointer.cast();

  /// Whether [dispose] ran.
  bool get isDisposed => _pointer == nullptr;

  /// What the probe measured so far.
  AudIoProbeResult read() {
    if (isDisposed) {
      throw const AudIoException(
        core.AUD_ERROR_STATE,
        'The probe is disposed.',
      );
    }
    final native = calloc<bindings.AudIoProbeResult>();
    try {
      native.ref.struct_size = sizeOf<bindings.AudIoProbeResult>();
      AudIoException.check(
        bindings.aud_io_probe_read(_pointer, native),
        'read the probe',
      );
      return AudIoProbeResult.fromNative(native.ref);
    } finally {
      calloc.free(native);
    }
  }

  /// Destroys the probe; no stream may render with it any more.
  void dispose() {
    if (isDisposed) return;
    bindings.aud_io_probe_destroy(_pointer);
    _pointer = nullptr;
  }

  late Pointer<bindings.AudIoProbe> _pointer;
}
