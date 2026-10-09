// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:async';
import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:ffi/ffi.dart';
import 'package:meta/meta.dart';

import 'aud_io_default_platform.dart'
    if (dart.library.ui) 'android/aud_io_flutter_platform.dart'
    as current;
import 'aud_audio_io_bindings_generated.dart' as bindings;
import 'aud_io_device.dart';
import 'aud_io_direction.dart';
import 'aud_io_exception.dart';
import 'aud_io_notification.dart';
import 'aud_io_permission.dart';
import 'aud_io_platform.dart';
import 'aud_io_stream.dart';
import 'aud_io_stream_config.dart';

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
class AudIoSession {
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
  AudIoSession({
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
  }) : platform = platform ?? current.currentPlatform() {
    final config = calloc<bindings.AudIoSessionConfig>();
    try {
      config.ref
        ..struct_size = sizeOf<bindings.AudIoSessionConfig>()
        ..backend = backend.code
        ..directions = directions.code
        ..flags =
            (mixWithOthers ? bindings.AUD_IO_SESSION_MIX_WITH_OTHERS : 0) |
            (bluetoothHfp ? bindings.AUD_IO_SESSION_BLUETOOTH_HFP : 0) |
            (measurement ? bindings.AUD_IO_SESSION_MEASUREMENT : 0) |
            (manualClock ? bindings.AUD_IO_SESSION_MANUAL_CLOCK : 0)
        ..notification_capacity = notificationCapacity
        ..recovery_timeout_ms = recoveryTimeout.inMilliseconds;
      _pointer = bindings.aud_io_session_create(config);
    } finally {
      calloc.free(config);
    }
    if (_pointer == nullptr) {
      throw const AudIoException(
        bindings.AUD_IO_ERROR_DEVICE,
        'The session was not created; check the configuration.',
      );
    }
    _buffer = calloc<bindings.AudIoNotification>(_batch);
    if (listen) {
      _listener = NativeCallable<Void Function(Pointer<Void>)>.listener(
        _onWake,
      );
      bindings.aud_io_session_set_listener(
        _pointer,
        _listener!.nativeFunction,
        nullptr,
      );
    }
    this.platform.attach(this);
  }

  // ...........................................................................
  /// What the platform adds to the native backend.
  final AudIoPlatform platform;

  /// The native session.
  Pointer<bindings.AudIoSession> get pointer => _pointer;

  /// Whether [dispose] ran.
  bool get isDisposed => _pointer == nullptr;

  /// The name of the backend, e.g. `oboe` or `null`.
  String get backendName {
    _checkAlive();
    return bindings
        .aud_io_session_backend_name(_pointer)
        .cast<Utf8>()
        .toDartString();
  }

  /// The streams that are open.
  List<AudIoStream> get streams => List.unmodifiable(_streams);

  // ...........................................................................
  // Devices and permission

  /// The devices of the platform.
  List<AudIoDevice> get devices {
    _checkAlive();
    final fromPlatform = platform.devices();
    if (fromPlatform != null) return fromPlatform;
    final count = AudIoException.check(
      bindings.aud_io_session_devices(_pointer, nullptr, 0),
      'list the devices',
    );
    final native = calloc<bindings.AudIoDevice>(count + 1);
    try {
      final listed = bindings.aud_io_session_devices(_pointer, native, count);
      return List.unmodifiable([
        for (var i = 0; i < listed && i < count; i++)
          AudIoDevice.fromNative(native[i]),
      ]);
    } finally {
      calloc.free(native);
    }
  }

  /// The devices whenever they change.
  Stream<List<AudIoDevice>> get deviceChanges => notifications
      .where((n) => n.type == AudIoNotificationType.devicesChanged)
      .map((_) => devices);

  /// The microphone permission.
  AudIoPermission get permission {
    _checkAlive();
    final fromPlatform = platform.permission();
    if (fromPlatform != null) return fromPlatform;
    return AudIoPermission.fromCode(
      AudIoException.check(
        bindings.aud_io_session_permission(_pointer),
        'read the permission',
      ),
    );
  }

  /// Asks the user for the microphone permission and returns the answer.
  Future<AudIoPermission> requestPermission() {
    _checkAlive();
    final fromPlatform = platform.requestPermission();
    if (fromPlatform != null) return fromPlatform;
    final answer = notifications
        .firstWhere(
          (n) =>
              n.type == AudIoNotificationType.permission &&
              n.permission != AudIoPermission.undetermined,
        )
        .then((n) => n.permission);
    AudIoException.check(
      bindings.aud_io_session_request_permission(_pointer),
      'ask for the permission',
    );
    return answer;
  }

  // ...........................................................................
  // Streams

  /// Opens a stream that calls [render] with [user] from its audio thread;
  /// see [AudIoStream.open].
  AudIoStream open(
    AudIoStreamConfig config, {
    required core.AudRenderFunction render,
    Pointer<Void>? user,
  }) => AudIoStream.open(this, config, render: render, user: user);

  /// Takes [stream] into the streams of the session; [AudIoStream.open]
  /// calls it.
  @internal
  void adopt(AudIoStream stream) => _streams.add(stream);

  /// Drops [stream] from the streams of the session; [AudIoStream.close]
  /// calls it.
  @internal
  void release(AudIoStream stream) => _streams.remove(stream);

  // ...........................................................................
  // Interruptions

  /// Tells the streams that the system took the audio, e.g. the audio
  /// focus went on Android; running streams stop until [resume].
  void interrupt([AudIoReason reason = AudIoReason.focusLoss]) {
    _checkAlive();
    AudIoException.check(
      bindings.aud_io_session_interrupt(_pointer, reason.code),
      'interrupt the session',
    );
  }

  /// Ends an interruption: the streams that ran start again.
  void resume() {
    _checkAlive();
    AudIoException.check(
      bindings.aud_io_session_resume(_pointer),
      'resume the session',
    );
  }

  /// Reports that the devices of the platform changed; [platform] calls it.
  void reportDevicesChanged() => _dispatch(
    const AudIoNotification(type: AudIoNotificationType.devicesChanged),
  );

  // ...........................................................................
  // Notifications

  /// The notifications of the session and its streams, as [pump] takes
  /// them.
  Stream<AudIoNotification> get notifications => _notifications.stream;

  /// Takes the waiting notifications and adds them to [notifications];
  /// returns their number. The listener calls it when the native side
  /// wakes it.
  int pump() {
    if (isDisposed) return 0;
    var total = 0;
    while (true) {
      final count = AudIoException.check(
        bindings.aud_io_session_take_notifications(_pointer, _buffer, _batch),
        'take the notifications',
      );
      for (var i = 0; i < count; i++) {
        _dispatch(AudIoNotification.fromNative(_buffer[i]));
      }
      total += count;
      if (count < _batch) return total;
    }
  }

  // ...........................................................................
  // The null device

  /// Injects [fault] with [value] into the null devices.
  void inject(AudIoFault fault, [int value = 0]) {
    _checkAlive();
    AudIoException.check(
      bindings.aud_io_null_inject(_pointer, fault.code, value),
      'inject ${fault.name}',
    );
  }

  /// Lets the null devices call back in the cycle of [sizes] from their
  /// next start; empty restores their buffer size.
  void setBlockSizes(List<int> sizes) {
    _checkAlive();
    final native = calloc<Uint32>(sizes.length + 1);
    try {
      native.asTypedList(sizes.length).setAll(0, sizes);
      AudIoException.check(
        bindings.aud_io_null_set_block_sizes(_pointer, native, sizes.length),
        'set the block sizes',
      );
    } finally {
      calloc.free(native);
    }
  }

  // ...........................................................................
  /// Closes the streams and the session.
  void dispose() {
    if (isDisposed) return;
    platform.detach();
    for (final stream in List.of(_streams)) {
      stream.close();
    }
    if (_listener != null) {
      bindings.aud_io_session_set_listener(_pointer, nullptr, nullptr);
    }
    bindings.aud_io_session_destroy(_pointer);
    _pointer = nullptr;
    _listener?.close();
    _listener = null;
    calloc.free(_buffer);
    unawaited(_notifications.close());
  }

  // ...........................................................................
  static const int _batch = 32;
  late Pointer<bindings.AudIoSession> _pointer;
  late final Pointer<bindings.AudIoNotification> _buffer;
  NativeCallable<Void Function(Pointer<Void>)>? _listener;
  final List<AudIoStream> _streams = [];
  final StreamController<AudIoNotification> _notifications =
      StreamController.broadcast();

  void _onWake(Pointer<Void> user) => pump();

  void _dispatch(AudIoNotification notification) {
    if (!_notifications.isClosed) _notifications.add(notification);
  }

  void _checkAlive() {
    if (isDisposed) {
      throw const AudIoException(
        core.AUD_ERROR_STATE,
        'The session is disposed.',
      );
    }
  }
}
