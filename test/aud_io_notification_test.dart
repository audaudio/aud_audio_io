// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoNotificationType', () {
    test('fromCode(code) finds every type and refuses others', () {
      expect([
        for (final type in AudIoNotificationType.values)
          AudIoNotificationType.fromCode(type.code),
      ], AudIoNotificationType.values);
      expect(
        () => AudIoNotificationType.fromCode(99),
        throwsA(isA<ArgumentError>()),
      );
    });
  });

  group('AudIoReason', () {
    test('fromCode(code) finds every reason and maps others to none', () {
      expect([
        for (final reason in AudIoReason.values)
          AudIoReason.fromCode(reason.code),
      ], AudIoReason.values);
      expect(AudIoReason.fromCode(99), AudIoReason.none);
    });
  });

  group('AudIoNotification', () {
    test('permission reads the code of a permission notification', () {
      final notification = AudIoNotification(
        type: AudIoNotificationType.permission,
        code: AudIoPermission.denied.code,
      );
      expect(notification.permission, AudIoPermission.denied);
    });
  });

  group('AudIoNotification', () {
    test('elapsed and toString()', () {
      const notification = AudIoNotification(
        type: AudIoNotificationType.recovered,
        streamId: 2,
        generation: 1,
        value: 3000000,
      );
      expect(notification.elapsed, const Duration(milliseconds: 3));
      expect(notification.toString(), contains('recovered, stream 2'));
    });
  });
}
