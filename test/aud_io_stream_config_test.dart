// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoStreamConfig', () {
    test('defaults to a low-latency output following the device', () {
      const config = AudIoStreamConfig();
      expect(config.direction, AudIoDirection.output);
      expect(config.sampleRate, 0);
      expect(config.performanceMode, AudIoPerformanceMode.lowLatency);
      expect(config.followFormat, isFalse);
    });
  });
}
