// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:async';

import 'package:aud_audio_core/aud_audio_core_ffi.dart';
import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:test/test.dart';

// A platform that answers for the native backend, as Android's does.
class _Platform extends AudIoPlatform {
  _Platform() : super.native();

  final calls = <String>[];
  AudIoSession? attached;

  @override
  List<AudIoDevice>? devices() => const [
    AudIoDevice(
      id: '2',
      name: 'Speaker',
      directions: AudIoDirection.output,
      route: AudIoRoute.speaker,
    ),
  ];

  @override
  AudIoPermission? permission() => AudIoPermission.denied;

  @override
  Future<AudIoPermission>? requestPermission() async => AudIoPermission.granted;

  @override
  void attach(AudIoSession session) {
    calls.add('attach');
    attached = session;
  }

  @override
  void detach() => calls.add('detach');
}

void main() {
  group('AudIoSession', () {
    late AudIoSessionFfi session;

    setUp(() {
      session = AudIoSessionFfi(
        backend: AudIoBackend.nullDevice,
        directions: AudIoDirection.duplex,
        manualClock: true,
      );
    });

    tearDown(() => session.dispose());

    Future<AudIoNotification> next(AudIoNotificationType type) => session
        .notifications
        .firstWhere((n) => n.type == type)
        .timeout(const Duration(seconds: 5));

    test('lists the null devices', () {
      expect(session.backendName, 'null');
      expect(session.isDisposed, isFalse);
      expect(session.pointer.address, isNot(0));
      expect(session.platform, isA<AudIoPlatform>());
      expect(session.streams, isEmpty);
      expect(session.devices, const [
        AudIoDevice(
          id: 'null:out',
          name: 'Null output',
          directions: AudIoDirection.output,
          route: AudIoRoute.virtual,
          isDefaultOutput: true,
          maxOutputChannels: 2,
        ),
        AudIoDevice(
          id: 'null:in',
          name: 'Null input',
          directions: AudIoDirection.input,
          route: AudIoRoute.virtual,
          isDefaultInput: true,
          maxInputChannels: 2,
        ),
      ]);
    });

    test('deviceChanges lists the devices after a hot-plug', () async {
      final changed = session.deviceChanges.first.timeout(
        const Duration(seconds: 5),
      );
      session.inject(AudIoFault.hotPlug, 4);
      final devices = await changed;
      expect(devices.map((d) => d.id), ['null:out', 'null:in', 'null:usb:1']);
      expect(devices.last.route, AudIoRoute.usb);
    });

    test('permission and requestPermission() follow the user', () async {
      expect(session.permission, AudIoPermission.granted);
      session.inject(AudIoFault.permission, AudIoPermission.denied.code);
      expect(session.permission, AudIoPermission.denied);
      expect(await session.requestPermission(), AudIoPermission.denied);
      session.inject(AudIoFault.permission, AudIoPermission.undetermined.code);
      expect(await session.requestPermission(), AudIoPermission.granted);
    });

    test('interrupt() and resume() stop and restart the streams', () async {
      final threaded = AudIoSessionFfi(backend: AudIoBackend.nullDevice);
      addTearDown(threaded.dispose);
      final stream = threaded.open(
        const AudIoStreamConfig(),
        render: AudIoStreamFfi.sineRender,
      );
      stream.start();
      final interrupted = threaded.notifications.firstWhere(
        (n) => n.type == AudIoNotificationType.interrupted,
      );
      threaded.interrupt();
      expect((await interrupted).reason, AudIoReason.focusLoss);
      expect(stream.state, AudIoState.interrupted);
      final resumed = threaded.notifications.firstWhere(
        (n) => n.type == AudIoNotificationType.resumed,
      );
      threaded.resume();
      await resumed;
      expect(stream.state, AudIoState.running);
    });

    test('reportDevicesChanged() reaches the notifications', () async {
      final changed = next(AudIoNotificationType.devicesChanged);
      session.reportDevicesChanged();
      expect((await changed).streamId, 0);
    });

    test('pump() takes the notifications without a listener', () async {
      final quiet = AudIoSessionFfi(
        backend: AudIoBackend.nullDevice,
        listen: false,
        notificationCapacity: 64,
        recoveryTimeout: const Duration(seconds: 1),
      );
      final seen = <AudIoNotification>[];
      final subscription = quiet.notifications.listen(seen.add);
      for (var i = 0; i < 40; i++) {
        quiet.inject(AudIoFault.hotPlug, 1);
      }
      expect(quiet.pump(), 40);
      await pumpEventQueue();
      expect(seen.length, 40);
      quiet.dispose();
      expect(quiet.pump(), 0);
      await subscription.cancel();
    });

    test('inject() and setBlockSizes() refuse invalid values', () {
      expect(
        () => session.inject(AudIoFault.late, 0),
        throwsA(
          isA<AudIoException>()
              .having((e) => e.code, 'code', AUD_ERROR_INVALID_ARGUMENT)
              .having((e) => e.message, 'message', contains('inject late')),
        ),
      );
      session.setBlockSizes([64, 128]);
      session.setBlockSizes([]);
      expect(() => session.setBlockSizes([0]), throwsA(isA<AudIoException>()));
      for (final fault in AudIoFault.values) {
        expect(fault.code, greaterThan(0));
      }
      expect(AudIoBackend.platform.code, bindings.AUD_IO_BACKEND_PLATFORM);
    });

    test('dispose() closes the streams and refuses further calls', () {
      final stream = session.open(
        const AudIoStreamConfig(),
        render: AudIoStreamFfi.sineRender,
      );
      expect(session.streams, [stream]);
      session.dispose();
      expect(session.isDisposed, isTrue);
      expect(stream.isClosed, isTrue);
      session.dispose();
      final calls = <void Function()>[
        () => session.backendName,
        () => session.devices,
        () => session.permission,
        () => session.requestPermission(),
        () => session.interrupt(),
        () => session.resume(),
        () => session.inject(AudIoFault.route),
        () => session.setBlockSizes([]),
        () => session.open(
          const AudIoStreamConfig(),
          render: AudIoStreamFfi.sineRender,
        ),
      ];
      for (final call in calls) {
        expect(
          call,
          throwsA(
            isA<AudIoException>().having(
              (e) => e.message,
              'message',
              'The session is disposed.',
            ),
          ),
        );
      }
      session.reportDevicesChanged();
    });

    test('the platform answers before the native backend', () async {
      final platform = _Platform();
      final android = AudIoSessionFfi(
        backend: AudIoBackend.nullDevice,
        platform: platform,
      );
      expect(platform.attached, android);
      expect(android.devices.single.id, '2');
      expect(android.permission, AudIoPermission.denied);
      expect(await android.requestPermission(), AudIoPermission.granted);
      android.dispose();
      expect(platform.calls, ['attach', 'detach']);
    });
  });
}
