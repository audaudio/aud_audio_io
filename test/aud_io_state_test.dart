// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoState', () {
    test('fromCode(code) finds every value and refuses others', () {
      expect([
        for (final value in AudIoState.values) AudIoState.fromCode(value.code),
      ], AudIoState.values);
      expect(
        () => AudIoState.fromCode(99),
        throwsA(
          isA<ArgumentError>().having(
            (e) => e.message,
            'message',
            contains('Unknown state'),
          ),
        ),
      );
    });
  });
}
