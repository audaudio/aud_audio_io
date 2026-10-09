// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoRoute', () {
    test('fromCode(code) finds every route and maps others to unknown', () {
      expect([
        for (final route in AudIoRoute.values) AudIoRoute.fromCode(route.code),
      ], AudIoRoute.values);
      expect(AudIoRoute.fromCode(99), AudIoRoute.unknown);
    });
  });
}
