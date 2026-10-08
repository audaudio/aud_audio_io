// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('aud_audio_io_bindings_generated.dart', () {
    test('aud_io_sine_render fills every channel with a sine', () {
      final output = calloc<Float>(480 * 2);
      try {
        bindings.aud_io_sine_render(nullptr, output, 480, 2);
        final samples = output.asTypedList(960);
        expect(samples.any((s) => s != 0), isTrue);
        expect(samples.reduce((a, b) => a > b ? a : b), closeTo(0.1, 0.001));
        expect(samples[100], samples[101], reason: 'both channels alike');
      } finally {
        calloc.free(output);
      }
    });

    test('null streams are tolerated by the C API', () {
      final stats = calloc<bindings.AudIoStats>();
      try {
        expect(bindings.aud_io_open(nullptr, nullptr, nullptr), nullptr);
        expect(bindings.aud_io_start(nullptr), lessThan(0));
        expect(bindings.aud_io_stop(nullptr), lessThan(0));
        expect(bindings.aud_io_sample_rate(nullptr), 0);
        expect(bindings.aud_io_channels(nullptr), 0);
        expect(bindings.aud_io_frames_per_callback(nullptr), 0);
        expect(
          bindings.aud_io_backend_name(nullptr).cast<Utf8>().toDartString(),
          '',
        );
        bindings.aud_io_get_stats(nullptr, stats);
        bindings.aud_io_reset_stats(nullptr);
        bindings.aud_io_close(nullptr);
      } finally {
        calloc.free(stats);
      }
    });
  });
}
