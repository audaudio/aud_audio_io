// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:async';

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:aud_audio_io/src/android/aud_io_android_api.dart';
import 'package:aud_audio_io/src/android/aud_io_android_platform.dart';
import 'package:test/test.dart';

// The Java side as a test sets it up.
class _Api implements AudIoAndroidApi {
  List<AudIoAndroidDeviceInfo> infos = [];
  List<int> media = [];
  bool granted = false;
  bool grantOnRequest = false;
  int permissionRequests = 0;
  int focusResult = AudIoAndroid.focusRequestGranted;
  int focusRequests = 0;
  int focusAbandons = 0;
  void Function(int change)? onChange;

  @override
  int get sdkLevel => 36;

  @override
  List<AudIoAndroidDeviceInfo> devices() => infos;

  @override
  List<int> mediaDeviceIds() => media;

  @override
  bool get hasRecordPermission => granted;

  @override
  void requestRecordPermission() {
    permissionRequests++;
    if (grantOnRequest) granted = true;
  }

  @override
  int requestFocus(void Function(int change) onChange) {
    focusRequests++;
    this.onChange = onChange;
    return focusResult;
  }

  @override
  void abandonFocus() => focusAbandons++;
}

const _speaker = AudIoAndroidDeviceInfo(
  id: 2,
  name: 'Speaker',
  type: 2,
  isSink: true,
  isSource: false,
  channelCounts: [1, 2],
  sampleRates: [48000],
);
const _earpiece = AudIoAndroidDeviceInfo(
  id: 1,
  name: 'Earpiece',
  type: 1,
  isSink: true,
  isSource: false,
);
const _mic = AudIoAndroidDeviceInfo(
  id: 3,
  name: 'Mic',
  type: 15,
  isSink: false,
  isSource: true,
  channelCounts: [1],
);
const _usbOut = AudIoAndroidDeviceInfo(
  id: 10,
  name: 'UMC',
  type: 22,
  isSink: true,
  isSource: false,
  channelCounts: [2, 4],
);
const _usbIn = AudIoAndroidDeviceInfo(
  id: 11,
  name: 'UMC',
  type: 22,
  isSink: false,
  isSource: true,
  channelCounts: [2],
);
const _telephony = AudIoAndroidDeviceInfo(
  id: 9,
  name: 'Telephony',
  type: 18,
  isSink: true,
  isSource: true,
);

void main() {
  group('AudIoAndroidPlatform', () {
    late _Api api;
    late StreamController<void> resumed;
    late AudIoAndroidPlatform platform;

    setUp(() {
      api = _Api();
      resumed = StreamController<void>.broadcast();
      platform = AudIoAndroidPlatform(
        api,
        resumed: resumed.stream,
        pollInterval: const Duration(milliseconds: 5),
        permissionTimeout: const Duration(milliseconds: 50),
      );
    });

    tearDown(() => resumed.close());

    AudIoSessionFfi session() {
      final created = AudIoSessionFfi(
        backend: AudIoBackend.nullDevice,
        platform: platform,
      );
      addTearDown(created.dispose);
      return created;
    }

    Future<void> until(bool Function() condition) async {
      final deadline = DateTime.now().add(const Duration(seconds: 5));
      while (!condition()) {
        if (DateTime.now().isAfter(deadline)) fail('timed out');
        await Future<void>.delayed(const Duration(milliseconds: 2));
      }
    }

    test('routeOf(type) maps the types of AudioDeviceInfo', () {
      expect(
        {
          for (final type in [
            1, 2, 24, 3, 4, 5, 6, 13, 19, 31, 7, 8, 9, 10, 29, 11, 12, 22, //
            15, 21, 23, 26, 27, 30, 0, 99,
          ])
            type: AudIoAndroidPlatform.routeOf(type),
        },
        {
          1: AudIoRoute.receiver,
          2: AudIoRoute.speaker,
          24: AudIoRoute.speaker,
          3: AudIoRoute.wiredHeadset,
          4: AudIoRoute.wiredHeadphones,
          5: AudIoRoute.line,
          6: AudIoRoute.line,
          13: AudIoRoute.line,
          19: AudIoRoute.line,
          31: AudIoRoute.line,
          7: AudIoRoute.bluetoothHfp,
          8: AudIoRoute.bluetoothA2dp,
          9: AudIoRoute.hdmi,
          10: AudIoRoute.hdmi,
          29: AudIoRoute.hdmi,
          11: AudIoRoute.usb,
          12: AudIoRoute.usb,
          22: AudIoRoute.usb,
          15: AudIoRoute.builtinMic,
          21: AudIoRoute.car,
          23: AudIoRoute.hearingAid,
          26: AudIoRoute.bluetoothLe,
          27: AudIoRoute.bluetoothLe,
          30: AudIoRoute.bluetoothLe,
          0: AudIoRoute.unknown,
          99: AudIoRoute.unknown,
        },
      );
    });

    test('devices() lists outputs and inputs with their defaults', () {
      api.infos = [_earpiece, _speaker, _mic, _usbOut, _usbIn, _telephony];
      expect(platform.devices(), [
        const AudIoDevice(
          id: '1',
          name: 'Earpiece',
          directions: AudIoDirection.output,
          route: AudIoRoute.receiver,
        ),
        const AudIoDevice(
          id: '2',
          name: 'Speaker',
          directions: AudIoDirection.output,
          route: AudIoRoute.speaker,
          maxOutputChannels: 2,
          sampleRates: [48000],
        ),
        const AudIoDevice(
          id: '10',
          name: 'UMC',
          directions: AudIoDirection.output,
          route: AudIoRoute.usb,
          isDefaultOutput: true,
          maxOutputChannels: 4,
        ),
        const AudIoDevice(
          id: '3',
          name: 'Mic',
          directions: AudIoDirection.input,
          route: AudIoRoute.builtinMic,
          maxInputChannels: 1,
        ),
        const AudIoDevice(
          id: '11',
          name: 'UMC',
          directions: AudIoDirection.input,
          route: AudIoRoute.usb,
          isDefaultInput: true,
          maxInputChannels: 2,
        ),
      ]);
      // From API 33 the system names where media plays.
      api.media = [2];
      final speaker = platform.devices().firstWhere((d) => d.id == '2');
      expect(speaker.isDefaultOutput, isTrue);
      expect(speaker.isActive, isTrue);
      // Without a device of the priority list the first one is the default.
      api
        ..media = []
        ..infos = [
          const AudIoAndroidDeviceInfo(
            id: 40,
            name: 'Bus',
            type: 21,
            isSink: true,
            isSource: false,
          ),
        ];
      expect(platform.devices().single.isDefaultOutput, isTrue);
      api.infos = [];
      expect(platform.devices(), isEmpty);
    });

    test('permission() and requestPermission() follow the user', () async {
      expect(platform.permission(), AudIoPermission.undetermined);
      api.grantOnRequest = true;
      final granted = platform.requestPermission()!;
      resumed.add(null);
      expect(await granted, AudIoPermission.granted);
      expect(platform.permission(), AudIoPermission.granted);
      expect(await platform.requestPermission(), AudIoPermission.granted);
      expect(api.permissionRequests, 1);
      // Refused: the answer comes when the app returns, or never.
      api
        ..granted = false
        ..grantOnRequest = false;
      final refused = platform.requestPermission()!;
      resumed
        ..add(null)
        ..add(null);
      expect(await refused, AudIoPermission.denied);
      expect(platform.permission(), AudIoPermission.denied);
      expect(await platform.requestPermission(), AudIoPermission.denied);
    });

    test('attach() reports hot-plugs it polls', () async {
      api.infos = [_speaker];
      final audio = session();
      final changed = audio.deviceChanges.first;
      api.infos = [_speaker, _usbOut];
      final devices = await changed.timeout(const Duration(seconds: 5));
      expect(devices.map((d) => d.id), ['2', '10']);
      expect(audio.devices.length, 2);
    });

    test('a disconnect reads the devices at once', () async {
      api.infos = [_speaker];
      final slow = AudIoAndroidPlatform(
        api,
        pollInterval: const Duration(hours: 1),
      );
      final audio = AudIoSessionFfi(
        backend: AudIoBackend.nullDevice,
        platform: slow,
      );
      addTearDown(audio.dispose);
      final stream = audio.open(
        const AudIoStreamConfig(),
        render: AudIoStreamFfi.sineRender,
      );
      stream.start();
      final changed = audio.deviceChanges.first;
      api.infos = [_usbOut];
      audio.inject(AudIoFault.disconnect);
      expect(
        (await changed.timeout(const Duration(seconds: 5))).single.id,
        '10',
      );
    });

    test('holds the audio focus while a stream plays', () async {
      final audio = session();
      final stream = audio.open(
        const AudIoStreamConfig(),
        render: AudIoStreamFfi.sineRender,
      );
      stream.start();
      await until(() => platform.hasFocus);
      expect(api.focusRequests, 1);

      // A call takes the focus for a while.
      api.onChange!(AudIoAndroid.focusLossTransient);
      await until(() => stream.state == AudIoState.interrupted);
      api.onChange!(AudIoAndroid.focusLossTransientCanDuck);
      api.onChange!(AudIoAndroid.focusGain);
      await until(() => stream.state == AudIoState.running);

      // Another app takes it for good; the return of the app asks again.
      api.onChange!(AudIoAndroid.focusLoss);
      await until(() => stream.state == AudIoState.interrupted);
      resumed.add(null);
      await until(() => stream.state == AudIoState.running);
      expect(api.focusRequests, 2);
      resumed.add(null); // nothing lost, nothing to ask

      stream.stop();
      await until(() => !platform.hasFocus);
      expect(api.focusAbandons, 1);
    });

    test('waits for a focus the system gives later', () async {
      api.focusResult = AudIoAndroid.focusRequestDelayed;
      final audio = session();
      final stream = audio.open(
        const AudIoStreamConfig(),
        render: AudIoStreamFfi.sineRender,
      );
      stream.start();
      await until(() => stream.state == AudIoState.interrupted);
      api.onChange!(AudIoAndroid.focusGain);
      await until(() => stream.state == AudIoState.running);
    });

    test('asks again for a refused focus when the app returns', () async {
      api.focusResult = AudIoAndroid.focusRequestFailed;
      final audio = session();
      final stream = audio.open(
        const AudIoStreamConfig(),
        render: AudIoStreamFfi.sineRender,
      );
      stream.start();
      await until(() => stream.state == AudIoState.interrupted);
      resumed.add(null);
      await Future<void>.delayed(const Duration(milliseconds: 20));
      expect(stream.state, AudIoState.interrupted);
      api.focusResult = AudIoAndroid.focusRequestGranted;
      resumed.add(null);
      await until(() => stream.state == AudIoState.running);
    });

    test('detach() releases what the resumes came from', () {
      var detached = 0;
      final releasing = AudIoAndroidPlatform(api, onDetach: () => detached++);
      final audio = AudIoSessionFfi(
        backend: AudIoBackend.nullDevice,
        platform: releasing,
        listen: false,
      );
      audio.dispose();
      expect(detached, 1);
    });

    test('detach() gives the focus back and stops listening', () async {
      final audio = session();
      final stream = audio.open(
        const AudIoStreamConfig(),
        render: AudIoStreamFfi.sineRender,
      );
      stream.start();
      await until(() => platform.hasFocus);
      final onChange = api.onChange!;
      audio.dispose();
      expect(platform.hasFocus, isFalse);
      expect(api.focusAbandons, 1);
      onChange(AudIoAndroid.focusGain); // after the session: ignored
      resumed.add(null);
      platform.detach();
    });
  });
}
