// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoSession', () {
    test('creates the session of the platform', () {
      final session = AudIoSession(
        backend: AudIoBackend.nullDevice,
        manualClock: true,
        listen: false,
      );
      addTearDown(session.dispose);
      expect(session.isDisposed, isFalse);
      expect(session.streams, isEmpty);
    });

    test('names the backends and faults by their codes', () {
      expect(AudIoBackend.nullDevice.code, AUD_IO_BACKEND_NULL);
      expect(AudIoFault.failStart.code, AUD_IO_FAULT_FAIL_START);
    });
  });
}
