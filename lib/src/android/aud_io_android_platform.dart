// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:async';

import '../aud_io_device.dart';
import '../aud_io_direction.dart';
import '../aud_io_notification.dart';
import '../aud_io_permission.dart';
import '../aud_io_platform.dart';
import '../aud_io_route.dart';
import '../aud_io_session.dart';
import '../aud_io_state.dart';
import 'aud_io_android_api.dart';

// #############################################################################
/// The Android platform of a session (ticket 21, decision 1): the devices of
/// `AudioManager`, polled for hot-plugs because `AudioDeviceCallback` is an
/// abstract class Dart cannot extend; the audio focus while a stream runs,
/// whose loss interrupts the session and whose return resumes it; and the
/// microphone permission.
class AudIoAndroidPlatform extends AudIoPlatform {
  /// Creates the platform.
  ///
  /// - [api] reaches Java
  /// - [resumed] fires when the app returns to the foreground
  /// - [pollInterval] how often the devices are read for hot-plugs
  /// - [permissionTimeout] how long [requestPermission] waits for an answer
  /// - [onDetach] releases what [resumed] came from, e.g. a lifecycle
  ///   listener
  AudIoAndroidPlatform(
    this.api, {
    Stream<void> resumed = const Stream.empty(),
    this.pollInterval = const Duration(seconds: 1),
    this.permissionTimeout = const Duration(minutes: 1),
    this._onDetach,
  }) : _resumed = resumed.asBroadcastStream(),
       super.native();

  // ...........................................................................
  /// Reaches Java.
  final AudIoAndroidApi api;

  /// How often the devices are read for hot-plugs.
  final Duration pollInterval;

  /// How long [requestPermission] waits for the user.
  final Duration permissionTimeout;

  /// Whether the session holds the audio focus.
  bool get hasFocus => _focus;

  // ...........................................................................
  /// The route of an `AudioDeviceInfo` type.
  static AudIoRoute routeOf(int type) => switch (type) {
    1 => AudIoRoute.receiver,
    2 || 24 => AudIoRoute.speaker,
    3 => AudIoRoute.wiredHeadset,
    4 => AudIoRoute.wiredHeadphones,
    5 || 6 || 13 || 19 || 31 => AudIoRoute.line,
    7 => AudIoRoute.bluetoothHfp,
    8 => AudIoRoute.bluetoothA2dp,
    9 || 10 || 29 => AudIoRoute.hdmi,
    11 || 12 || 22 => AudIoRoute.usb,
    15 => AudIoRoute.builtinMic,
    21 => AudIoRoute.car,
    23 => AudIoRoute.hearingAid,
    26 || 27 || 30 => AudIoRoute.bluetoothLe,
    _ => AudIoRoute.unknown,
  };

  // ...........................................................................
  @override
  List<AudIoDevice> devices() {
    final infos = api.devices().where((i) => !_internal.contains(i.type));
    final media = api.mediaDeviceIds().toSet();
    final outputs = infos.where((i) => i.isSink).toList();
    final inputs = infos.where((i) => i.isSource).toList();
    final defaultOutput = media.isNotEmpty
        ? outputs.where((i) => media.contains(i.id)).firstOrNull
        : _first(outputs, _outputPriority);
    final defaultInput = _first(inputs, _inputPriority);
    AudIoDevice device(AudIoAndroidDeviceInfo info, bool output) {
      final channels = info.channelCounts.fold(0, (a, b) => a > b ? a : b);
      return AudIoDevice(
        id: '${info.id}',
        name: info.name,
        directions: output ? AudIoDirection.output : AudIoDirection.input,
        route: routeOf(info.type),
        isDefaultOutput: output && info == defaultOutput,
        isDefaultInput: !output && info == defaultInput,
        isActive: output && media.contains(info.id),
        maxOutputChannels: output ? channels : 0,
        maxInputChannels: output ? 0 : channels,
        sampleRates: List.unmodifiable([
          for (final rate in info.sampleRates) rate.toDouble(),
        ]),
      );
    }

    return List.unmodifiable([
      for (final info in outputs) device(info, true),
      for (final info in inputs) device(info, false),
    ]);
  }

  @override
  AudIoPermission? permission() {
    if (api.hasRecordPermission) return AudIoPermission.granted;
    return _asked ? AudIoPermission.denied : AudIoPermission.undetermined;
  }

  @override
  Future<AudIoPermission>? requestPermission() => _request();

  // ...........................................................................
  @override
  void attach(AudIoSession session) {
    _session = session;
    _signature = _signatureOf(api.devices());
    _poll = Timer.periodic(pollInterval, (_) => _tick());
    _subscriptions
      ..add(session.notifications.listen(_onNotification))
      ..add(_resumed.listen((_) => _onResumed()));
  }

  @override
  void detach() {
    _poll?.cancel();
    _poll = null;
    for (final subscription in _subscriptions) {
      unawaited(subscription.cancel());
    }
    _subscriptions.clear();
    if (_focus) api.abandonFocus();
    _focus = false;
    _session = null;
    _onDetach?.call();
  }

  // ...........................................................................
  AudIoSession? _session;
  final Stream<void> _resumed;
  final void Function()? _onDetach;
  final List<StreamSubscription<void>> _subscriptions = [];
  Timer? _poll;
  String _signature = '';
  bool _asked = false;
  bool _focus = false;
  bool _focusLost = false;

  // Devices the system uses inside: telephony and the remote submix.
  static const _internal = {18, 25};

  // Where Android plays by default, in order.
  static const _outputPriority = [22, 11, 12, 3, 4, 26, 27, 8, 7, 9, 2];

  // Where Android records by default, in order.
  static const _inputPriority = [22, 11, 12, 3, 15];

  static AudIoAndroidDeviceInfo? _first(
    List<AudIoAndroidDeviceInfo> infos,
    List<int> priority,
  ) {
    for (final type in priority) {
      final match = infos.where((i) => i.type == type).firstOrNull;
      if (match != null) return match;
    }
    return infos.firstOrNull;
  }

  static String _signatureOf(List<AudIoAndroidDeviceInfo> infos) =>
      [for (final i in infos) '${i.id}:${i.type}:${i.isSink}'].join(',');

  void _tick() {
    final signature = _signatureOf(api.devices());
    if (signature != _signature) {
      _signature = signature;
      _session?.reportDevicesChanged();
    }
    _updateFocus();
  }

  void _onNotification(AudIoNotification notification) {
    if (notification.type == AudIoNotificationType.disconnected) _tick();
    _updateFocus();
  }

  // Holds the focus while a stream wants to play.
  void _updateFocus() {
    final session = _session;
    if (session == null) return;
    final wanted = session.streams.any(
      (s) => !s.isClosed && s.state != AudIoState.stopped,
    );
    if (wanted && !_focus) {
      _focus = true;
      final result = api.requestFocus(_onFocusChange);
      if (result != AudIoAndroid.focusRequestGranted) {
        // Later, or not before the next return to the foreground.
        _focusLost = result == AudIoAndroid.focusRequestFailed;
        session.interrupt();
      }
    } else if (!wanted && _focus) {
      _focus = false;
      _focusLost = false;
      api.abandonFocus();
    }
  }

  void _onFocusChange(int change) {
    final session = _session;
    if (session == null) return;
    switch (change) {
      case AudIoAndroid.focusGain:
        _focusLost = false;
        session.resume();
      case AudIoAndroid.focusLoss:
        // Another app took it for good; the next return asks again.
        _focusLost = true;
        session.interrupt();
      case AudIoAndroid.focusLossTransient:
        session.interrupt();
    }
  }

  void _onResumed() {
    final session = _session;
    if (session == null || !_focusLost) return;
    if (api.requestFocus(_onFocusChange) == AudIoAndroid.focusRequestGranted) {
      _focusLost = false;
      session.resume();
    }
  }

  Future<AudIoPermission> _request() async {
    if (api.hasRecordPermission) return AudIoPermission.granted;
    _asked = true;
    final answered = Completer<void>();
    final subscription = _resumed.listen((_) {
      if (!answered.isCompleted) answered.complete();
    });
    api.requestRecordPermission();
    await answered.future.timeout(permissionTimeout, onTimeout: () {});
    await subscription.cancel();
    return api.hasRecordPermission
        ? AudIoPermission.granted
        : AudIoPermission.denied;
  }
}
