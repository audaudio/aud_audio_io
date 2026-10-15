// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  AudIoCounters counters({
    int callbacks = 0,
    int periods = 0,
    int fastCpus = 0,
    int cpuSamples = 0,
    int fastCpuSamples = 0,
  }) => AudIoCounters(
    state: AudIoState.running,
    callbacks: callbacks,
    frames: 0,
    renders: 0,
    callbackFramesMin: 0,
    callbackFramesMax: 0,
    periodMinNs: 0,
    periodMaxNs: 0,
    periodSumNs: 4000,
    periodCount: periods,
    lateCallbacks: 0,
    xruns: 0,
    disconnects: 0,
    recoveries: 0,
    interruptions: 0,
    heldBlocks: 0,
    renderErrors: 0,
    notificationsDropped: 0,
    callbackTimeMaxNs: 0,
    callbackTimeSumNs: 600,
    recoveryTimeMaxNs: 0,
    recoveryTimeLastNs: 0,
    hostTimeJitterMaxNs: 0,
    lastTime: const AudStreamTime(
      frames: 0,
      sampleRate: 48000,
      samplePosition: 0,
    ),
    fastCpus: fastCpus,
    cpuSamples: cpuSamples,
    fastCpuSamples: fastCpuSamples,
  );

  group('AudIoCounters', () {
    test('the means divide the sums by the counts', () {
      expect(counters(callbacks: 3, periods: 2).periodMeanNs, 2000);
      expect(counters(callbacks: 3, periods: 2).callbackTimeMeanNs, 200);
    });

    test('the means are 0 without callbacks', () {
      expect(counters().periodMeanNs, 0);
      expect(counters().callbackTimeMeanNs, 0);
    });

    test('the placement defaults to the system', () {
      expect(counters().placement, AudIoPlacement.system);
      expect(counters().placementResets, 0);
      expect(counters().placementRefusals, 0);
    });

    test('fastCpuShare is the share of the samples on a fast core', () {
      expect(
        counters(fastCpus: 0xf0, cpuSamples: 8, fastCpuSamples: 6).fastCpuShare,
        0.75,
      );
    });

    test('fastCpuShare is null without samples or fast cores', () {
      expect(counters(fastCpus: 0xf0).fastCpuShare, isNull);
      expect(counters(cpuSamples: 8, fastCpuSamples: 0).fastCpuShare, isNull);
    });
  });
}
