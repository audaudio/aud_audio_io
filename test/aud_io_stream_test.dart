// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';
import 'dart:math';
import 'dart:typed_data';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:aud_audio_graph/aud_audio_graph.dart';
// The graph's render function is the one a client hands the stream.
// ignore: implementation_imports
import 'package:aud_audio_graph/src/aud_audio_graph_bindings_generated.dart'
    as graph_bindings;
import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoStream', () {
    late AudIoSession session;

    setUp(() {
      session = AudIoSession(
        backend: AudIoBackend.nullDevice,
        directions: AudIoDirection.duplex,
        manualClock: true,
      );
    });

    tearDown(() => session.dispose());

    Future<AudIoNotification> next(
      AudIoStream stream,
      AudIoNotificationType type,
    ) => stream.notifications
        .firstWhere((n) => n.type == type)
        .timeout(const Duration(seconds: 5));

    Future<void> waitFor(AudIoStream stream, AudIoState state) async {
      final deadline = DateTime.now().add(const Duration(seconds: 5));
      while (stream.state != state) {
        if (DateTime.now().isAfter(deadline)) fail('never ${state.name}');
        await Future<void>.delayed(const Duration(milliseconds: 1));
      }
    }

    double peak(Float32List samples) =>
        samples.fold(0, (a, b) => max(a, b.abs()));

    test('open() gets a format and runs the sine', () async {
      final stream = session.open(
        const AudIoStreamConfig(maxFrames: 128),
        render: AudIoStream.sineRender,
      );
      expect(stream.id, 1);
      expect(stream.pointer.address, isNot(0));
      expect(stream.session, session);
      expect(stream.state, AudIoState.stopped);
      final format = stream.format;
      expect(format.direction, AudIoDirection.output);
      expect(format.sampleRate, 48000);
      expect(format.outputChannels, 2);
      expect(format.inputChannels, 0);
      expect(format.maxFrames, 128);
      expect(format.bufferFrames, 256);
      expect(format.generation, 1);
      expect(format.timeSource, AudTimeSource.hardware);
      expect(format.outputDeviceId, 'null:out');
      expect(format.backend, 'null');
      final started = next(stream, AudIoNotificationType.started);
      stream.start();
      expect(stream.state, AudIoState.running);
      final output = stream.debugProcess(
        frames: 480,
        hostTimeNs: AudClock.nowNs(),
      );
      expect(output.length, 960);
      expect(peak(output), closeTo(0.1, 0.001));
      expect((await started).elapsed, Duration.zero);
      final counters = stream.counters;
      expect(counters.state, AudIoState.running);
      expect(counters.callbacks, 1);
      expect(counters.renders, 4);
      expect(counters.lastTime.hostTimeSource, AudTimeSource.hardware);
      stream.resetCounters();
      expect(stream.counters.callbacks, 0);
      final stopped = next(stream, AudIoNotificationType.stopped);
      stream.stop();
      expect((await stopped).reason, AudIoReason.request);
    });

    test('a duplex stream hands the input to the render function', () {
      final stream = session.open(
        const AudIoStreamConfig(direction: AudIoDirection.duplex),
        render: AudIoStream.thruRender,
      );
      stream.start();
      final input = Float32List.fromList([
        for (var i = 0; i < 64; i++) i / 100,
      ]);
      final output = stream.debugProcess(
        frames: 32,
        hostTimeNs: AudClock.nowNs(),
        input: input,
      );
      expect(output, input);
    });

    test('an input stream has no output', () {
      final stream = session.open(
        const AudIoStreamConfig(direction: AudIoDirection.input),
        render: AudIoStream.thruRender,
      );
      stream.start();
      expect(
        stream.debugProcess(frames: 32, hostTimeNs: AudClock.nowNs()),
        isEmpty,
      );
      expect(stream.format.inputDeviceId, 'null:in');
    });

    test('refuses what the state does not allow', () {
      final stream = session.open(
        const AudIoStreamConfig(),
        render: AudIoStream.sineRender,
      );
      expect(
        stream.stop,
        throwsA(
          isA<AudIoException>().having((e) => e.code, 'code', AUD_ERROR_STATE),
        ),
      );
      expect(
        () => stream.debugProcess(frames: 1, hostTimeNs: 0),
        throwsA(isA<AudIoException>()),
      );
      stream.start();
      expect(
        stream.start,
        throwsA(
          isA<AudIoException>().having(
            (e) => e.message,
            'message',
            'Could not start the stream: AUD_ERROR_STATE',
          ),
        ),
      );
      expect(() => stream.acknowledge(7), throwsA(isA<AudIoException>()));
    });

    test('open() refuses an unknown device', () {
      expect(
        () => session.open(
          const AudIoStreamConfig(outputDeviceId: 'nowhere'),
          render: AudIoStream.sineRender,
        ),
        throwsA(
          isA<AudIoException>().having(
            (e) => e.codeName,
            'codeName',
            'AUD_IO_ERROR_NO_DEVICE',
          ),
        ),
      );
      expect(session.streams, isEmpty);
    });

    test(
      'holds the renderer after a format change until acknowledged',
      () async {
        final stream = session.open(
          const AudIoStreamConfig(),
          render: AudIoStream.sineRender,
        );
        stream.start();
        var host = AudClock.nowNs();
        stream.debugProcess(frames: 256, hostTimeNs: host);
        final changed = next(stream, AudIoNotificationType.formatChanged);
        session.inject(AudIoFault.sampleRate, 44100);
        final notification = await changed;
        expect(notification.generation, 2);
        expect(notification.sampleRate, 44100);
        await waitFor(stream, AudIoState.running);
        host += 100000000;
        final held = stream.debugProcess(frames: 256, hostTimeNs: host);
        expect(peak(held), 0);
        expect(stream.counters.heldBlocks, 1);
        stream.acknowledge(notification.generation);
        host += 100000000;
        final played = stream.debugProcess(frames: 256, hostTimeNs: host);
        expect(peak(played), closeTo(0.1, 0.002));
        expect(stream.format.sampleRate, 44100);
      },
    );

    test('close() closes once and refuses further calls', () {
      final stream = session.open(
        const AudIoStreamConfig(),
        render: AudIoStream.sineRender,
      );
      stream.start();
      stream.close();
      expect(stream.isClosed, isTrue);
      expect(session.streams, isEmpty);
      stream.close();
      final calls = <void Function()>[
        () => stream.state,
        () => stream.format,
        () => stream.counters,
        stream.start,
        stream.stop,
        () => stream.acknowledge(1),
        stream.resetCounters,
        () => stream.debugProcess(frames: 1, hostTimeNs: 0),
      ];
      for (final call in calls) {
        expect(
          call,
          throwsA(
            isA<AudIoException>().having(
              (e) => e.message,
              'message',
              'The stream is closed.',
            ),
          ),
        );
      }
    });

    test('AudIoStream.open() refuses a disposed session', () {
      session.dispose();
      expect(
        () => AudIoStream.open(
          session,
          const AudIoStreamConfig(),
          render: AudIoStream.sineRender,
        ),
        throwsA(isA<AudIoException>()),
      );
    });

    group('with an AudGraph', () {
      int risingCrossings(Float32List samples) {
        var count = 0;
        for (var i = 1; i < samples.length; i++) {
          if (samples[i - 1] < 0 && samples[i] >= 0) count++;
        }
        return count;
      }

      test('renders the graph and runs its route-change sequence', () async {
        final graph = AudGraph(maxFrames: 256, outputChannels: const [1]);
        addTearDown(graph.dispose);
        final osc = graph.createNode('aud.graph.oscillator');
        graph.transaction((tx) => tx.connect(osc, graph.io));
        final stream = session.open(
          AudIoStreamConfig(outputChannels: 1, maxFrames: graph.maxFrames),
          render:
              Native.addressOf<NativeFunction<core.AudRenderFunctionFunction>>(
                graph_bindings.aud_graph_render,
              ),
          user: graph.pointer.cast(),
        );
        graph.prepare(
          sampleRate: stream.format.sampleRate,
          maxFrames: stream.format.maxFrames,
        );
        graph.start();
        graph.transport(const AudTransportRequest.start());
        stream.start();

        // One second at 48 kHz in device callbacks of 480 frames.
        var host = AudClock.nowNs();
        Float32List play(double rate) {
          final second = Float32List(rate.round());
          for (var done = 0; done < second.length; done += 480) {
            final frames = min(480, second.length - done);
            second.setAll(
              done,
              stream.debugProcess(frames: frames, hostTimeNs: host),
            );
            host += (frames * 1e9 / rate).round();
          }
          return second;
        }

        final before = play(48000);
        expect(risingCrossings(before), inInclusiveRange(439, 441));
        final beat = graph.transportState.beat;
        expect(beat, greaterThan(0));

        // The device switches to 44.1 kHz: the client runs the graph's
        // route-change sequence and acknowledges the new format.
        final changed = next(stream, AudIoNotificationType.formatChanged);
        session.inject(AudIoFault.sampleRate, 44100);
        final notification = await changed;
        graph.suspend();
        graph.prepare(
          sampleRate: stream.format.sampleRate,
          maxFrames: stream.format.maxFrames,
        );
        graph.resume();
        stream.acknowledge(notification.generation);
        await waitFor(stream, AudIoState.running);

        final after = play(44100);
        expect(graph.sampleRate, 44100);
        expect(risingCrossings(after), inInclusiveRange(439, 441));
        // The transport kept its position and runs on.
        expect(graph.transportState.beat, greaterThan(beat));
        // The device was open again within the 500 ms of lifecycle-001,
        // measured on the host clock; the manual clock of the callbacks
        // runs ahead of it.
        expect(
          notification.elapsed,
          lessThan(const Duration(milliseconds: 500)),
        );
        expect(stream.counters.renderErrors, 0);
      });
    });
  });
}
