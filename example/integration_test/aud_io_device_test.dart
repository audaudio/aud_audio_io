// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_io/aud_audio_io.dart';
// The Android platform is internal; the test reads its focus.
// ignore: implementation_imports
import 'package:aud_audio_io/src/android/aud_io_android_platform.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

// Runs the platform backend on a simulator, an emulator or a device:
// `flutter test integration_test -d <device>`. It proves that the code runs
// there; the budgets need the reference devices (ticket 21).
void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  late AudIoSession session;

  setUp(() => session = AudIoSession(directions: AudIoDirection.duplex));
  tearDown(() => session.dispose());

  Future<AudIoCounters> play(AudIoStream stream) async {
    stream.start();
    await Future<void>.delayed(const Duration(seconds: 2));
    final counters = stream.counters;
    stream.stop();
    return counters;
  }

  testWidgets('lists the devices of the platform', (tester) async {
    final devices = session.devices;
    expect(devices.where((d) => d.directions.hasOutput), isNotEmpty);
    // ignore: avoid_print
    print('backend ${session.backendName}: $devices');
  });

  testWidgets('plays an output stream with stream times', (tester) async {
    final stream = session.open(
      const AudIoStreamConfig(),
      render: AudIoStream.sineRender,
    );
    final format = stream.format;
    final counters = await play(stream);
    // ignore: avoid_print
    print(
      '$format\ncallbacks ${counters.callbacks}, '
      'frames ${counters.callbackFramesMin}..${counters.callbackFramesMax}, '
      'period ${(counters.periodMeanNs / 1e6).toStringAsFixed(2)} ms, '
      'late ${counters.lateCallbacks}, xruns ${counters.xruns}, '
      'latency out ${counters.lastTime.outputLatencyFrames}, '
      'source ${counters.lastTime.hostTimeSource.name}, '
      'jitter max ${counters.hostTimeJitterMaxNs} ns',
    );
    expect(format.sampleRate, greaterThan(0));
    expect(format.outputChannels, greaterThan(0));
    expect(counters.callbacks, greaterThan(10));
    expect(counters.renderErrors, 0);
    expect(counters.lastTime.hostTimeSource, isNot(AudTimeSource.none));
    expect(counters.lastTime.sampleRate, format.sampleRate);
  });

  testWidgets('runs a duplex stream when the microphone is allowed', (
    tester,
  ) async {
    if (session.permission != AudIoPermission.granted) {
      markTestSkipped('no microphone permission on this device');
      return;
    }
    final stream = session.open(
      const AudIoStreamConfig(direction: AudIoDirection.duplex),
      render: AudIoStream.thruRender,
    );
    final format = stream.format;
    final counters = await play(stream);
    // ignore: avoid_print
    print(
      '$format\ncallbacks ${counters.callbacks}, '
      'frames ${counters.callbackFramesMin}..${counters.callbackFramesMax}, '
      'latency out ${counters.lastTime.outputLatencyFrames} '
      'in ${counters.lastTime.inputLatencyFrames}, '
      'xruns ${counters.xruns}, state ${counters.state.name}',
    );
    expect(counters.callbacks, greaterThan(10));
    expect(format.inputChannels, greaterThan(0));
  });

  testWidgets('holds the audio focus on Android while a stream plays', (
    tester,
  ) async {
    if (!Platform.isAndroid) {
      markTestSkipped('the audio focus is Android');
      return;
    }
    final platform = session.platform as AudIoAndroidPlatform;
    final stream = session.open(
      const AudIoStreamConfig(),
      render: AudIoStream.sineRender,
    );
    stream.start();
    await Future<void>.delayed(const Duration(milliseconds: 500));
    expect(platform.hasFocus, isTrue);
    stream.stop();
    await Future<void>.delayed(const Duration(milliseconds: 500));
    expect(platform.hasFocus, isFalse);
  });
}
