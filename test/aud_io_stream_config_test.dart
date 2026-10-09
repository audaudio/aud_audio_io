// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoStreamConfig', () {
    test('toNative(arena, render, user) writes every field', () {
      using((arena) {
        const config = AudIoStreamConfig(
          direction: AudIoDirection.duplex,
          outputDeviceId: '17',
          inputDeviceId: '18',
          outputChannels: 4,
          inputChannels: 2,
          sampleRate: 48000,
          bufferFrames: 96,
          maxFrames: 512,
          performanceMode: AudIoPerformanceMode.powerSaving,
          followFormat: true,
        );
        final native = config
            .toNative(
              arena,
              render: AudIoStream.sineRender,
              user: Pointer.fromAddress(8),
            )
            .ref;
        expect(native.struct_size, sizeOf<bindings.AudIoStreamConfig>());
        expect(native.direction, bindings.AUD_IO_DUPLEX);
        expect(native.output_device_id.cast<Utf8>().toDartString(), '17');
        expect(native.input_device_id.cast<Utf8>().toDartString(), '18');
        expect(native.output_channels, 4);
        expect(native.input_channels, 2);
        expect(native.sample_rate, 48000);
        expect(native.buffer_frames, 96);
        expect(native.max_frames, 512);
        expect(
          native.performance_mode,
          bindings.AUD_IO_PERFORMANCE_POWER_SAVING,
        );
        expect(
          native.flags,
          bindings.AUD_IO_STREAM_EXCLUSIVE |
              bindings.AUD_IO_STREAM_LATENCY_TUNER |
              bindings.AUD_IO_STREAM_FOLLOW_FORMAT,
        );
        expect(native.render, AudIoStream.sineRender);
        expect(native.render_user.address, 8);
      });
    });

    test('leaves the defaults at 0 and null', () {
      using((arena) {
        final native = const AudIoStreamConfig(
          exclusive: false,
          latencyTuner: false,
        ).toNative(arena, render: AudIoStream.sineRender).ref;
        expect(native.direction, bindings.AUD_IO_OUTPUT);
        expect(native.output_device_id, nullptr);
        expect(native.input_device_id, nullptr);
        expect(native.output_channels, 0);
        expect(native.sample_rate, 0);
        expect(native.max_frames, 0);
        expect(native.flags, 0);
        expect(native.render_user, nullptr);
      });
    });
  });
}
