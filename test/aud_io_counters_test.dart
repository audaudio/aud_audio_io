// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoCounters', () {
    test('fromNative(native) reads every field', () {
      final native = calloc<bindings.AudIoCounters>();
      addTearDown(() => calloc.free(native));
      var value = 1;
      native.ref
        ..state = bindings.AUD_IO_STATE_RECOVERING
        ..callbacks = 10
        ..frames = value++
        ..renders = value++
        ..callback_frames_min = value++
        ..callback_frames_max = value++
        ..period_min_ns = value++
        ..period_max_ns = value++
        ..period_sum_ns = 900
        ..period_count = 9
        ..late_callbacks = value++
        ..xruns = value++
        ..disconnects = value++
        ..recoveries = value++
        ..interruptions = value++
        ..held_blocks = value++
        ..render_errors = value++
        ..notifications_dropped = value++
        ..callback_time_max_ns = value++
        ..callback_time_sum_ns = 50
        ..recovery_time_max_ns = value++
        ..recovery_time_last_ns = value++
        ..host_time_jitter_max_ns = value++;
      native.ref.last_time
        ..frames = 64
        ..sample_rate = 48000
        ..sample_position = 128
        ..host_time_source = AUD_TIME_SOURCE_ESTIMATED;
      final counters = AudIoCounters.fromNative(native.ref);
      expect(counters.state, AudIoState.recovering);
      expect(
        [
          counters.frames,
          counters.renders,
          counters.callbackFramesMin,
          counters.callbackFramesMax,
          counters.periodMinNs,
          counters.periodMaxNs,
          counters.lateCallbacks,
          counters.xruns,
          counters.disconnects,
          counters.recoveries,
          counters.interruptions,
          counters.heldBlocks,
          counters.renderErrors,
          counters.notificationsDropped,
          counters.callbackTimeMaxNs,
          counters.recoveryTimeMaxNs,
          counters.recoveryTimeLastNs,
          counters.hostTimeJitterMaxNs,
        ],
        [for (var i = 1; i <= 18; i++) i],
      );
      expect(counters.callbacks, 10);
      expect(counters.periodSumNs, 900);
      expect(counters.periodCount, 9);
      expect(counters.periodMeanNs, 100);
      expect(counters.callbackTimeSumNs, 50);
      expect(counters.callbackTimeMeanNs, 5);
      expect(counters.lastTime.frames, 64);
      expect(counters.lastTime.samplePosition, 128);
      expect(counters.lastTime.hostTimeSource, AudTimeSource.estimated);
    });

    test('the means are 0 without callbacks', () {
      final native = calloc<bindings.AudIoCounters>();
      addTearDown(() => calloc.free(native));
      final counters = AudIoCounters.fromNative(native.ref);
      expect(counters.periodMeanNs, 0);
      expect(counters.callbackTimeMeanNs, 0);
    });
  });
}
