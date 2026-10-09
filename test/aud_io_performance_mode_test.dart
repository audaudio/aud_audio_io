// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoPerformanceMode', () {
    test('fromCode(code) finds every value and refuses others', () {
      expect([
        for (final value in AudIoPerformanceMode.values)
          AudIoPerformanceMode.fromCode(value.code),
      ], AudIoPerformanceMode.values);
      expect(
        () => AudIoPerformanceMode.fromCode(99),
        throwsA(
          isA<ArgumentError>().having(
            (e) => e.message,
            'message',
            contains('Unknown mode'),
          ),
        ),
      );
    });
  });
}
