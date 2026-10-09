// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoPlatform.native()', () {
    test('leaves everything to the native backend', () {
      const platform = AudIoPlatform.native();
      final session = AudIoSession(
        backend: AudIoBackend.nullDevice,
        listen: false,
        platform: platform,
      );
      addTearDown(session.dispose);
      expect(platform.devices(), isNull);
      expect(platform.permission(), isNull);
      expect(platform.requestPermission(), isNull);
      platform.attach(session);
      platform.detach();
    });
  });
}
