// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:aud_audio_io/src/android/aud_io_flutter_platform.dart';
import 'package:test/test.dart';

void main() {
  group('currentPlatform()', () {
    test('leaves everything to the native backend off Android', () {
      final platform = currentPlatform();
      expect(platform, isA<AudIoPlatform>());
      expect(platform.devices(), isNull);
      expect(platform.permission(), isNull);
    });
  });
}
