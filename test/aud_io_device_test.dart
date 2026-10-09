// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoDevice', () {
    test('== and hashCode compare every field', () {
      const a = AudIoDevice(
        id: '1',
        name: 'A',
        directions: AudIoDirection.output,
        sampleRates: [48000],
      );
      const differing = [
        AudIoDevice(id: '2', name: 'A', directions: AudIoDirection.output),
        AudIoDevice(
          id: '1',
          name: 'A',
          directions: AudIoDirection.output,
          sampleRates: [44100],
        ),
        AudIoDevice(
          id: '1',
          name: 'A',
          directions: AudIoDirection.output,
          sampleRates: [48000, 96000],
        ),
      ];
      const same = AudIoDevice(
        id: '1',
        name: 'A',
        directions: AudIoDirection.output,
        sampleRates: [48000],
      );
      expect(a, same);
      expect(a.hashCode, same.hashCode);
      expect(
        [for (final other in differing) a == other],
        [false, false, false],
      );
      expect(a == Object(), isFalse);
    });
  });

  group('AudIoDevice', () {
    test('toString() names the device', () {
      expect(
        const AudIoDevice(
          id: '3',
          name: 'Speaker',
          directions: AudIoDirection.output,
        ).toString(),
        'AudIoDevice(3, Speaker, output, unknown)',
      );
    });
  });
}
