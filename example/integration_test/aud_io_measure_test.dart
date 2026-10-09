// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:convert';

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

// Measures a device for the plan of ticket 21:
//
//   flutter test integration_test/aud_io_measure_test.dart -d <device> \
//     --dart-define=AUD_SECONDS=60
//
// AUD_THRESHOLD sets the onset level of the probe, AUD_MEASUREMENT=false
// leaves iOS its processing of the input and the output.
//
// It plays a sine and reports the callbacks, then asks for the microphone
// and runs the latency probe through the device's speaker and microphone -
// or a cable from the output to the input - and reports the round trip
// against the latency the stream reports. Each report is a line
// `AUD_REPORT {...}`.
void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const seconds = int.fromEnvironment('AUD_SECONDS', defaultValue: 20);
  const threshold = String.fromEnvironment('AUD_THRESHOLD', defaultValue: '');
  // iOS: the measurement mode skips the processing of the input and the
  // output; the speaker of some devices plays much quieter in it.
  const measurement = bool.fromEnvironment(
    'AUD_MEASUREMENT',
    defaultValue: true,
  );

  double ms(num ns) => (ns / 1e4).round() / 100;

  Map<String, Object?> countersOf(AudIoStream stream) {
    final format = stream.format;
    final c = stream.counters;
    return {
      'backend': format.backend,
      'sampleRate': format.sampleRate,
      'outputChannels': format.outputChannels,
      'inputChannels': format.inputChannels,
      'bufferFrames': format.bufferFrames,
      'burstFrames': format.burstFrames,
      'timeSource': format.timeSource.name,
      'callbacks': c.callbacks,
      'callbackFramesMin': c.callbackFramesMin,
      'callbackFramesMax': c.callbackFramesMax,
      'periodMeanMs': ms(c.periodMeanNs),
      'periodMinMs': ms(c.periodMinNs),
      'periodMaxMs': ms(c.periodMaxNs),
      'lateCallbacks': c.lateCallbacks,
      'xruns': c.xruns,
      'callbackMeanMs': ms(c.callbackTimeMeanNs),
      'callbackMaxMs': ms(c.callbackTimeMaxNs),
      'accuracyMs': ms(c.lastTime.hostTimeAccuracyNs),
      'jitterMaxMs': ms(c.hostTimeJitterMaxNs),
      'outputLatencyFrames': c.lastTime.outputLatencyFrames,
      'inputLatencyFrames': c.lastTime.inputLatencyFrames,
      'disconnects': c.disconnects,
      'recoveries': c.recoveries,
      'renderErrors': c.renderErrors,
    };
  }

  void report(String name, Map<String, Object?> values) {
    // ignore: avoid_print
    print('AUD_REPORT ${jsonEncode({'name': name, ...values})}');
  }

  testWidgets('measures the device', (tester) async {
    final session = AudIoSessionFfi(
      directions: AudIoDirection.duplex,
      measurement: measurement,
    );
    addTearDown(session.dispose);
    report('devices', {
      'measurement': measurement,
      'backend': session.backendName,
      'devices': [for (final d in session.devices) d.toString()],
    });

    // The output alone.
    final output = session.open(
      const AudIoStreamConfig(followFormat: true),
      render: AudIoStreamFfi.sineRender,
    );
    output.start();
    await Future<void>.delayed(const Duration(seconds: seconds));
    report('output', countersOf(output));
    output.close();

    // The round trip through the device's speaker and microphone.
    // Someone has to answer the dialog on the device; without an answer
    // the probe is skipped instead of waiting for ever.
    var permission = session.permission;
    if (permission != AudIoPermission.granted) {
      permission = await session.requestPermission().timeout(
        const Duration(minutes: 1),
        onTimeout: () => AudIoPermission.undetermined,
      );
    }
    report('permission', {'permission': permission.name});
    if (permission != AudIoPermission.granted) return;
    for (final level
        in threshold.isEmpty
            ? const [0.1, 0.03, 0.01]
            : [double.parse(threshold)]) {
      final probe = AudIoProbe(intervalFrames: 24000, threshold: level);
      final duplex = session.open(
        const AudIoStreamConfig(
          direction: AudIoDirection.duplex,
          followFormat: true,
        ),
        render: AudIoProbe.render,
        user: probe.pointer,
      );
      duplex.start();
      await Future<void>.delayed(const Duration(seconds: seconds));
      final result = probe.read();
      final rate = duplex.format.sampleRate;
      report('probe', {
        'threshold': level,
        ...countersOf(duplex),
        'clicks': result.clicks,
        'detections': result.detections,
        'roundTripFrames': result.lastRoundTripFrames,
        'roundTripMinFrames': result.minRoundTripFrames,
        'roundTripMaxFrames': result.maxRoundTripFrames,
        'roundTripMeanFrames': result.meanRoundTripFrames,
        'reportedFrames': result.reportedRoundTripFrames,
        'inputPeak': (result.inputPeak * 1000).round() / 1000,
        'errorFrames': result.errorFrames,
        'errorMs': rate == 0
            ? 0
            : (result.errorFrames * 1e5 / rate).round() / 100,
      });
      duplex.close();
      probe.dispose();
      if (result.detections > 0) break;
    }
  }, timeout: const Timeout(Duration(minutes: 30)));
}
