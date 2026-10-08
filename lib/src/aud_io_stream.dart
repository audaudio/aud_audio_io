// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:ffi/ffi.dart';

import 'aud_audio_io_bindings_generated.dart' as bindings;

// #############################################################################
/// Thrown when a stream cannot be opened, started or stopped.
class AudIoException implements Exception {
  /// Creates the exception for a result [code] and a [message].
  const AudIoException({required this.code, required this.message});

  /// The result code, one of the `AUD_ERROR_*` constants.
  final int code;

  /// What failed.
  final String message;

  @override
  String toString() => 'AudIoException($code): $message';
}

// #############################################################################
/// The counters of the stream callback.
class AudIoStats {
  /// Creates a snapshot of the counters.
  const AudIoStats({
    required this.framesMin,
    required this.framesMax,
    required this.callbacks,
    required this.frames,
    required this.periodMinNs,
    required this.periodMaxNs,
    required this.periodSumNs,
    required this.periodCount,
    required this.lateCallbacks,
    required this.xruns,
    required this.disconnects,
    required this.callbackTimeMaxNs,
    required this.callbackTimeSumNs,
    required this.outputLatencyMs,
  });

  /// The smallest callback block.
  final int framesMin;

  /// The largest callback block.
  final int framesMax;

  /// Callbacks so far.
  final int callbacks;

  /// Frames rendered so far.
  final int frames;

  /// The shortest time between two callback starts, in nanoseconds.
  final int periodMinNs;

  /// The longest time between two callback starts, in nanoseconds.
  final int periodMaxNs;

  /// The sum of all periods, in nanoseconds.
  final int periodSumNs;

  /// The number of periods summed.
  final int periodCount;

  /// Callbacks that came later than 1.5 times the block duration.
  final int lateCallbacks;

  /// Underruns the backend reports; Oboe only.
  final int xruns;

  /// Times the backend closed the stream, e.g. on a route change.
  final int disconnects;

  /// The longest time spent inside the callback, in nanoseconds.
  final int callbackTimeMaxNs;

  /// The sum of the time spent inside the callback, in nanoseconds.
  final int callbackTimeSumNs;

  /// The output latency the backend reports in milliseconds; 0 if unknown.
  final double outputLatencyMs;

  /// The mean period between callbacks in nanoseconds.
  double get periodMeanNs => periodCount == 0 ? 0 : periodSumNs / periodCount;

  /// The mean time spent inside the callback in nanoseconds.
  double get callbackTimeMeanNs =>
      callbacks == 0 ? 0 : callbackTimeSumNs / callbacks;
}

// #############################################################################
/// An output stream of the spike: pulls interleaved float blocks from a
/// native render callback on the audio thread and measures the callback
/// timing. miniaudio on macOS, iOS, Windows and Linux, Oboe on Android.
class AudIoStream {
  /// Opens a stream that calls [render] with [user] from the audio thread.
  ///
  /// - [render] a native render callback, e.g. `AudEngine.renderCallback`
  ///   or [sineRender]
  /// - [user] the pointer handed to the callback, e.g. the engine handle
  /// - [sampleRate] in Hz; 0 takes the device's native rate
  /// - [channels] the output channels
  /// - [framesPerCallback] the block size; 0 takes the backend's default
  /// - [useNullBackend] renders into a silent device that keeps time, for
  ///   tests
  AudIoStream.open({
    required Pointer<NativeFunction<AudRenderCallbackFunction>> render,
    Pointer<Void>? user,
    double sampleRate = 0,
    int channels = 2,
    int framesPerCallback = 0,
    bool useNullBackend = false,
  }) {
    final config = calloc<bindings.AudIoConfig>();
    config.ref
      ..struct_size = sizeOf<bindings.AudIoConfig>()
      ..sample_rate = sampleRate
      ..channels = channels
      ..frames_per_callback = framesPerCallback
      ..use_null_backend = useNullBackend ? 1 : 0;
    _stream = bindings.aud_io_open(config, render, user ?? nullptr);
    calloc.free(config);
    if (_stream == nullptr) {
      throw const AudIoException(
        code: AUD_ERROR_FAILED,
        message:
            'The stream was not opened; check the device and the '
            'configuration.',
      );
    }
  }

  // ...........................................................................
  /// A render callback that plays a 440 Hz sine at -20 dBFS: the smoke
  /// signal of the package.
  static Pointer<NativeFunction<AudRenderCallbackFunction>> get sineRender =>
      Native.addressOf<NativeFunction<AudRenderCallbackFunction>>(
        bindings.aud_io_sine_render,
      );

  // ...........................................................................
  /// Starts the callbacks.
  void start() {
    _check(bindings.aud_io_start(_stream), 'start the stream');
    _running = true;
  }

  /// Stops the callbacks and waits for the last one to return.
  void stop() {
    _check(bindings.aud_io_stop(_stream), 'stop the stream');
    _running = false;
  }

  /// Closes the stream and the device; stops first when running.
  void close() {
    if (_stream == nullptr) return;
    if (_running) stop();
    bindings.aud_io_close(_stream);
    _stream = nullptr;
  }

  // ...........................................................................
  /// Whether the callbacks run.
  bool get isRunning => _running;

  /// The sample rate the device runs at.
  double get sampleRate => bindings.aud_io_sample_rate(_stream);

  /// The output channels.
  int get channels => bindings.aud_io_channels(_stream);

  /// The negotiated block size; 0 when the backend does not say.
  int get framesPerCallback => bindings.aud_io_frames_per_callback(_stream);

  /// The backend, e.g. `miniaudio/Core Audio` or `oboe/aaudio`.
  String get backendName =>
      bindings.aud_io_backend_name(_stream).cast<Utf8>().toDartString();

  /// A snapshot of the callback counters.
  AudIoStats get stats {
    final native = calloc<bindings.AudIoStats>();
    try {
      native.ref.struct_size = sizeOf<bindings.AudIoStats>();
      bindings.aud_io_get_stats(_stream, native);
      final s = native.ref;
      return AudIoStats(
        framesMin: s.frames_min,
        framesMax: s.frames_max,
        callbacks: s.callbacks,
        frames: s.frames,
        periodMinNs: s.period_min_ns,
        periodMaxNs: s.period_max_ns,
        periodSumNs: s.period_sum_ns,
        periodCount: s.period_count,
        lateCallbacks: s.late_callbacks,
        xruns: s.xruns,
        disconnects: s.disconnects,
        callbackTimeMaxNs: s.callback_time_max_ns,
        callbackTimeSumNs: s.callback_time_sum_ns,
        outputLatencyMs: s.output_latency_ms,
      );
    } finally {
      calloc.free(native);
    }
  }

  /// Zeroes the callback counters.
  void resetStats() => bindings.aud_io_reset_stats(_stream);

  // ...........................................................................
  late Pointer<bindings.AudIoStream> _stream;
  bool _running = false;

  void _check(int result, String what) {
    if (result < 0) {
      throw AudIoException(
        code: result,
        message: 'Could not $what: ${AudAbi.resultName(result)}',
      );
    }
  }
}
