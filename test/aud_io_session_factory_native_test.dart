// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
// ignore: implementation_imports
import 'package:aud_audio_io/src/aud_io_session_factory_native.dart';
import 'package:test/test.dart';

void main() {
  group('the native session factory', () {
    test('creates an AudIoSessionFfi', () {
      final session = createSession(
        backend: AudIoBackend.nullDevice,
        directions: AudIoDirection.output,
        mixWithOthers: false,
        bluetoothHfp: false,
        measurement: false,
        manualClock: true,
        notificationCapacity: 0,
        recoveryTimeout: Duration.zero,
        listen: false,
        platform: null,
      );
      addTearDown(session.dispose);
      expect(session, isA<AudIoSessionFfi>());
      expect(session.backendName, 'null');
    });
  });
}
