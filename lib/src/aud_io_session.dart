// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:async';

import 'aud_io_constants.dart' as bindings;
import 'aud_io_device.dart';
import 'aud_io_direction.dart';
import 'aud_io_notification.dart';
import 'aud_io_permission.dart';
import 'aud_io_platform.dart';
import 'aud_io_session_factory_stub.dart'
    if (dart.library.ffi) 'aud_io_session_factory_native.dart'
    as platform_factory;
import 'aud_io_stream.dart';

// #############################################################################
/// The backends of a session.
enum AudIoBackend {
  /// Oboe on Android, miniaudio with AVAudioSession on iOS; the null device
  /// on the desktop until its backends arrive (S3b to S3d).
  platform(bindings.AUD_IO_BACKEND_PLATFORM),

  /// A device that keeps time and takes injected faults, for tests.
  nullDevice(bindings.AUD_IO_BACKEND_NULL);

  const AudIoBackend(this.code);

  /// The `AUD_IO_BACKEND_*` code of the C API.
  final int code;
}

// #############################################################################
/// The faults a test injects into the null devices of a session.
enum AudIoFault {
  /// The device goes away; the value is the milliseconds until it is back.
  disconnect(bindings.AUD_IO_FAULT_DISCONNECT),

  /// The device switches to the rate in the value and goes away briefly.
  sampleRate(bindings.AUD_IO_FAULT_SAMPLE_RATE),

  /// The system takes the audio (value 1) or gives it back (value 0).
  interrupt(bindings.AUD_IO_FAULT_INTERRUPT),

  /// The next callback comes the value in milliseconds late.
  late(bindings.AUD_IO_FAULT_LATE),

  /// The backend reports the value as underruns.
  xrun(bindings.AUD_IO_FAULT_XRUN),

  /// A device with the value as channels is plugged in, or with 0 the last
  /// one is removed.
  hotPlug(bindings.AUD_IO_FAULT_HOT_PLUG),

  /// The next value opens of a device fail.
  failOpen(bindings.AUD_IO_FAULT_FAIL_OPEN),

  /// The microphone permission becomes the value, an
  /// [AudIoPermission.code].
  permission(bindings.AUD_IO_FAULT_PERMISSION),

  /// The route changes and the format stays.
  route(bindings.AUD_IO_FAULT_ROUTE),

  /// The default output gets the value as channels.
  channels(bindings.AUD_IO_FAULT_CHANNELS),

  /// The next value starts of a device fail although it opens.
  failStart(bindings.AUD_IO_FAULT_FAIL_START);

  const AudIoFault(this.code);

  /// The `AUD_IO_FAULT_*` code of the C API.
  final int code;
}

// #############################################################################
/// The audio session of an app: its devices, its streams and the
/// notifications they send. One per app; on iOS it configures and
/// activates the AVAudioSession.
///
/// The platform-neutral API: on native platforms the session is an
/// `AudIoSessionFfi` of `aud_audio_io_ffi.dart`, which opens the streams;
/// the web has no session until S5.
abstract interface class AudIoSession {
  /// Creates a session.
  ///
  /// - [backend] the platform's devices or the null device
  /// - [directions] what the app uses; iOS chooses the category by it
  /// - [mixWithOthers] iOS: mix with the audio of other apps
  /// - [bluetoothHfp] iOS: allow Bluetooth hands-free input at 16 kHz
  /// - [measurement] iOS: no processing of the input
  /// - [manualClock] null device: callbacks run only through
  ///   [AudIoStream.debugProcess]
  /// - [notificationCapacity] notifications waiting at most; 0 = 256
  /// - [recoveryTimeout] how long a recovery tries; zero = 5 seconds
  /// - [listen] take the notifications when the native side wakes; without
  ///   it, call [pump]
  /// - [platform] what the platform adds; null takes the current one's
  factory AudIoSession({
    AudIoBackend backend = AudIoBackend.platform,
    AudIoDirection directions = AudIoDirection.output,
    bool mixWithOthers = false,
    bool bluetoothHfp = false,
    bool measurement = false,
    bool manualClock = false,
    int notificationCapacity = 0,
    Duration recoveryTimeout = Duration.zero,
    bool listen = true,
    AudIoPlatform? platform,
  }) => platform_factory.createSession(
    backend: backend,
    directions: directions,
    mixWithOthers: mixWithOthers,
    bluetoothHfp: bluetoothHfp,
    measurement: measurement,
    manualClock: manualClock,
    notificationCapacity: notificationCapacity,
    recoveryTimeout: recoveryTimeout,
    listen: listen,
    platform: platform,
  );

  // ...........................................................................
  /// What the platform adds to the native backend.
  AudIoPlatform get platform;

  /// Whether [dispose] ran.
  bool get isDisposed;

  /// The name of the backend, e.g. `oboe` or `null`.
  String get backendName;

  /// The streams that are open.
  List<AudIoStream> get streams;

  /// The devices of the platform.
  List<AudIoDevice> get devices;

  /// The devices whenever they change.
  Stream<List<AudIoDevice>> get deviceChanges;

  /// The microphone permission.
  AudIoPermission get permission;

  /// Asks the user for the microphone permission and returns the answer.
  Future<AudIoPermission> requestPermission();

  /// Tells the streams that the system took the audio, e.g. the audio
  /// focus went on Android; running streams stop until [resume].
  void interrupt([AudIoReason reason = AudIoReason.focusLoss]);

  /// Ends an interruption: the streams that ran start again.
  void resume();

  /// Reports that the devices of the platform changed; [platform] calls it.
  void reportDevicesChanged();

  /// The notifications of the session and its streams, as [pump] takes
  /// them.
  Stream<AudIoNotification> get notifications;

  /// Takes the waiting notifications and adds them to [notifications];
  /// returns their number. The listener calls it when the native side
  /// wakes it.
  int pump();

  /// Injects [fault] with [value] into the null devices.
  void inject(AudIoFault fault, [int value = 0]);

  /// Lets the null devices call back in the cycle of [sizes] from their
  /// next start; empty restores their buffer size.
  void setBlockSizes(List<int> sizes);

  /// Closes the streams and the session.
  void dispose();
}
