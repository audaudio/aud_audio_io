// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoStream', () {
    test('is the platform-neutral face of a native stream', () {
      final session = AudIoSessionFfi(
        backend: AudIoBackend.nullDevice,
        manualClock: true,
        listen: false,
      );
      addTearDown(session.dispose);
      final AudIoStream stream = session.open(
        const AudIoStreamConfig(outputChannels: 1),
        render: AudIoStreamFfi.sineRender,
      );
      expect(stream.session, same(session));
      expect(stream.state, AudIoState.stopped);
      stream.close();
      expect(stream.isClosed, isTrue);
    });
  });
}
