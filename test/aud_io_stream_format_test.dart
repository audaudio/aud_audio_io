// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  AudIoStreamFormat format({double sampleRate = 48000}) => AudIoStreamFormat(
    direction: AudIoDirection.output,
    sampleRate: sampleRate,
    outputChannels: 2,
    inputChannels: 0,
    maxFrames: 1024,
    bufferFrames: 480,
    burstFrames: 96,
    generation: 1,
    performanceMode: AudIoPerformanceMode.lowLatency,
    exclusive: true,
    timeSource: AudTimeSource.estimated,
    outputDeviceId: '1',
    inputDeviceId: '',
    backend: 'null',
  );

  group('AudIoStreamFormat', () {
    test('bufferDuration is the buffer at the sample rate', () {
      expect(format().bufferDuration, const Duration(milliseconds: 10));
      expect(format(sampleRate: 0).bufferDuration, Duration.zero);
    });

    test('toString() names the format', () {
      expect(
        format().toString(),
        'AudIoStreamFormat(null, 48000 Hz, 2 out, 0 in, buffer 480, '
        'generation 1)',
      );
    });
  });
}
