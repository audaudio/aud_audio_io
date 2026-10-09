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
  group('AudIoStreamFormat', () {
    test('fromNative(native) reads every field', () {
      final native = calloc<bindings.AudIoStreamFormat>();
      addTearDown(() => calloc.free(native));
      native.ref
        ..direction = bindings.AUD_IO_DUPLEX
        ..sample_rate = 48000
        ..output_channels = 2
        ..input_channels = 1
        ..max_frames = 512
        ..buffer_frames = 192
        ..burst_frames = 96
        ..generation = 3
        ..performance_mode = bindings.AUD_IO_PERFORMANCE_LOW_LATENCY
        ..exclusive = 1
        ..time_source = AUD_TIME_SOURCE_HARDWARE;
      AudIoNativeString.write(native.ref.output_device_id, 128, '3');
      AudIoNativeString.write(native.ref.input_device_id, 128, '4');
      AudIoNativeString.write(native.ref.backend, 64, 'oboe/aaudio');
      final format = AudIoStreamFormat.fromNative(native.ref);
      expect(format.direction, AudIoDirection.duplex);
      expect(format.sampleRate, 48000);
      expect(format.outputChannels, 2);
      expect(format.inputChannels, 1);
      expect(format.maxFrames, 512);
      expect(format.bufferFrames, 192);
      expect(format.burstFrames, 96);
      expect(format.generation, 3);
      expect(format.performanceMode, AudIoPerformanceMode.lowLatency);
      expect(format.exclusive, isTrue);
      expect(format.timeSource, AudTimeSource.hardware);
      expect(format.outputDeviceId, '3');
      expect(format.inputDeviceId, '4');
      expect(format.backend, 'oboe/aaudio');
      expect(format.bufferDuration, const Duration(milliseconds: 4));
      expect(
        format.toString(),
        'AudIoStreamFormat(oboe/aaudio, 48000 Hz, 2 out, 1 in, buffer 192, '
        'generation 3)',
      );
      native.ref.sample_rate = 0;
      expect(
        AudIoStreamFormat.fromNative(native.ref).bufferDuration,
        Duration.zero,
      );
    });
  });
}
