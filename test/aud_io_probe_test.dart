// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoProbe', () {
    test('measures the round trip through the loop of the null device', () {
      final session = AudIoSession(
        backend: AudIoBackend.nullDevice,
        directions: AudIoDirection.duplex,
        manualClock: true,
        listen: false,
      );
      addTearDown(session.dispose);
      final probe = AudIoProbe(intervalFrames: 4800);
      final stream = session.open(
        const AudIoStreamConfig(direction: AudIoDirection.duplex),
        render: AudIoProbe.render,
        user: probe.pointer,
      );
      stream.start();
      var host = AudClock.nowNs();
      for (var i = 0; i < 100; i++) {
        stream.debugProcess(frames: 480, hostTimeNs: host);
        host += 10000000;
      }
      stream.close();
      final result = probe.read();
      expect(result.clicks, 10);
      expect(result.detections, greaterThanOrEqualTo(9));
      // The null device returns its output after its output plus input
      // latency, one buffer each.
      const roundTrip = 2 * 256;
      expect(result.lastRoundTripFrames, roundTrip);
      expect(result.minRoundTripFrames, roundTrip);
      expect(result.maxRoundTripFrames, roundTrip);
      expect(result.meanRoundTripFrames, roundTrip);
      expect(result.reportedRoundTripFrames, roundTrip);
      expect(result.errorFrames, 0);
      expect(result.inputPeak, closeTo(0.8, 0.01));
      probe.dispose();
      expect(probe.isDisposed, isTrue);
      probe.dispose();
      expect(
        probe.read,
        throwsA(
          isA<AudIoException>().having(
            (e) => e.message,
            'message',
            'The probe is disposed.',
          ),
        ),
      );
    });

    test('refuses an interval of 0', () {
      expect(
        () => AudIoProbe(intervalFrames: 0),
        throwsA(
          isA<AudIoException>().having(
            (e) => e.code,
            'code',
            AUD_ERROR_INVALID_ARGUMENT,
          ),
        ),
      );
    });

    test('AudIoProbeResult reads a result of the C API', () {
      final native = calloc<bindings.AudIoProbeResult>();
      addTearDown(() => calloc.free(native));
      final result = AudIoProbeResult.fromNative(native.ref);
      expect(result.detections, 0);
      expect(result.meanRoundTripFrames, 0);
    });
  });
}
