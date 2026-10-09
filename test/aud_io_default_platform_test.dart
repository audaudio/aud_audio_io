// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:aud_audio_io/src/aud_io_default_platform.dart';
import 'package:test/test.dart';

void main() {
  group('currentPlatform()', () {
    test('leaves everything to the native backend outside Flutter', () {
      final platform = currentPlatform();
      expect(platform, isA<AudIoPlatform>());
      expect(platform.devices(), isNull);
      final session = AudIoSession(
        backend: AudIoBackend.nullDevice,
        listen: false,
      );
      addTearDown(session.dispose);
      expect(session.devices.length, 2);
    });
  });
}
