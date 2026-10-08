// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoStream', () {
    late AudIoStream stream;

    setUp(() {
      stream = AudIoStream.open(
        render: AudIoStream.sineRender,
        useNullBackend: true,
        sampleRate: 48000,
        framesPerCallback: 256,
      );
    });

    tearDown(() => stream.close());

    test('opens the null backend with the requested format', () {
      expect(stream.backendName, 'miniaudio/Null');
      expect(stream.sampleRate, 48000);
      expect(stream.channels, 2);
      expect(stream.framesPerCallback, 256);
      expect(stream.isRunning, isFalse);
    });

    test('refuses an impossible configuration', () {
      expect(
        () => AudIoStream.open(
          render: AudIoStream.sineRender,
          useNullBackend: true,
          channels: 0,
        ),
        throwsA(
          isA<AudIoException>()
              .having((e) => e.code, 'code', AUD_ERROR_FAILED)
              .having((e) => e.toString(), 'toString', contains('opened')),
        ),
      );
    });

    test('runs the callback and measures its timing', () async {
      stream.start();
      expect(stream.isRunning, isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 300));
      final stats = stream.stats;
      expect(stats.callbacks, greaterThan(10));
      expect(stats.frames, greaterThanOrEqualTo(stats.callbacks * 256));
      expect(stats.framesMin, 256);
      expect(stats.framesMax, 256);
      expect(stats.periodCount, stats.callbacks - 1);
      expect(stats.periodMinNs, greaterThan(0));
      expect(stats.periodMaxNs, greaterThanOrEqualTo(stats.periodMinNs));
      expect(stats.periodMeanNs, closeTo(256 / 48000 * 1e9, 3e6));
      expect(stats.callbackTimeMaxNs, greaterThan(0));
      expect(stats.callbackTimeMeanNs, greaterThan(0));
      expect(stats.xruns, 0);
      expect(stats.disconnects, 0);
      expect(stats.outputLatencyMs, 0);
      stream.stop();
      expect(stream.isRunning, isFalse);
    });

    test('refuses to start twice or to stop when stopped', () {
      expect(
        () => stream.stop(),
        throwsA(
          isA<AudIoException>().having((e) => e.code, 'code', AUD_ERROR_STATE),
        ),
      );
      stream.start();
      expect(
        () => stream.start(),
        throwsA(
          isA<AudIoException>().having(
            (e) => e.message,
            'message',
            contains('AUD_ERROR_STATE'),
          ),
        ),
      );
    });

    test('resets the counters', () async {
      stream.start();
      await Future<void>.delayed(const Duration(milliseconds: 100));
      stream.resetStats();
      stream.stop();
      final stats = stream.stats;
      expect(stats.callbacks, lessThan(5));
      expect(stats.periodMeanNs, anyOf(0, greaterThan(0)));
      stream.close();
      expect(stream.stats.callbackTimeMeanNs, 0);
    });

    test('close() stops a running stream and can be called twice', () async {
      stream.start();
      stream.close();
      stream.close();
      expect(stream.isRunning, isFalse);
    });
  });
}
