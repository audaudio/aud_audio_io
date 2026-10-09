// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_audio_io_bindings_generated.dart' as bindings;
import 'aud_io_permission.dart';

// #############################################################################
/// What a notification reports.
enum AudIoNotificationType {
  /// Devices came or went.
  devicesChanged(bindings.AUD_IO_NOTIFY_DEVICES_CHANGED),

  /// The first callback ran after a start or a recovery.
  started(bindings.AUD_IO_NOTIFY_STARTED),

  /// The stream stopped on request.
  stopped(bindings.AUD_IO_NOTIFY_STOPPED),

  /// The device went away or changed; the stream recovers.
  disconnected(bindings.AUD_IO_NOTIFY_DISCONNECTED),

  /// The route changed and the format stayed.
  routeChanged(bindings.AUD_IO_NOTIFY_ROUTE_CHANGED),

  /// The stream reopened with a new rate or channel count and holds the
  /// renderer until the client acknowledges the new format.
  formatChanged(bindings.AUD_IO_NOTIFY_FORMAT_CHANGED),

  /// The system took the audio.
  interrupted(bindings.AUD_IO_NOTIFY_INTERRUPTED),

  /// The interruption ended.
  resumed(bindings.AUD_IO_NOTIFY_RESUMED),

  /// The recovery gave up.
  failed(bindings.AUD_IO_NOTIFY_FAILED),

  /// The microphone permission was answered.
  permission(bindings.AUD_IO_NOTIFY_PERMISSION),

  /// The render function returned an error.
  renderError(bindings.AUD_IO_NOTIFY_RENDER_ERROR),

  /// The stream reopened with the same format.
  recovered(bindings.AUD_IO_NOTIFY_RECOVERED);

  const AudIoNotificationType(this.code);

  /// The `AUD_IO_NOTIFY_*` code of the C API.
  final int code;

  // ...........................................................................
  /// The type with [code].
  static AudIoNotificationType fromCode(int code) => values.firstWhere(
    (type) => type.code == code,
    orElse: () => throw ArgumentError.value(code, 'code', 'Unknown type'),
  );
}

// #############################################################################
/// Why something happened.
enum AudIoReason {
  /// No reason given.
  none(bindings.AUD_IO_REASON_NONE),

  /// The device was removed.
  deviceRemoved(bindings.AUD_IO_REASON_DEVICE_REMOVED),

  /// A device was added.
  deviceAdded(bindings.AUD_IO_REASON_DEVICE_ADDED),

  /// A category change or an override of the route.
  routeOverride(bindings.AUD_IO_REASON_ROUTE_OVERRIDE),

  /// A call, an alarm or another app.
  system(bindings.AUD_IO_REASON_SYSTEM),

  /// iOS suspended the app.
  appSuspended(bindings.AUD_IO_REASON_APP_SUSPENDED),

  /// Android: the audio focus went.
  focusLoss(bindings.AUD_IO_REASON_FOCUS_LOSS),

  /// iOS reset its media services.
  mediaServicesReset(bindings.AUD_IO_REASON_MEDIA_SERVICES_RESET),

  /// The device changed its sample rate.
  sampleRate(bindings.AUD_IO_REASON_SAMPLE_RATE),

  /// The backend reported an error.
  error(bindings.AUD_IO_REASON_ERROR),

  /// The built-in microphone was muted, e.g. by a closed cover.
  builtinMicMuted(bindings.AUD_IO_REASON_BUILTIN_MIC_MUTED),

  /// The client asked for it.
  request(bindings.AUD_IO_REASON_REQUEST);

  const AudIoReason(this.code);

  /// The `AUD_IO_REASON_*` code of the C API.
  final int code;

  // ...........................................................................
  /// The reason with [code]; [none] for a code a newer backend reports.
  static AudIoReason fromCode(int code) =>
      values.firstWhere((reason) => reason.code == code, orElse: () => none);
}

// #############################################################################
/// A notification of a session or one of its streams.
class AudIoNotification {
  /// Creates a notification.
  const AudIoNotification({
    required this.type,
    this.streamId = 0,
    this.code = 0,
    this.reason = AudIoReason.none,
    this.generation = 0,
    this.hostTimeNs = 0,
    this.value = 0,
    this.sampleRate = 0,
    this.outputChannels = 0,
    this.inputChannels = 0,
  });

  /// Reads a notification of the C API.
  factory AudIoNotification.fromNative(bindings.AudIoNotification native) =>
      AudIoNotification(
        type: AudIoNotificationType.fromCode(native.type),
        streamId: native.stream,
        code: native.code,
        reason: AudIoReason.fromCode(native.reason),
        generation: native.generation,
        hostTimeNs: native.host_time_ns,
        value: native.value,
        sampleRate: native.sample_rate,
        outputChannels: native.output_channels,
        inputChannels: native.input_channels,
      );

  // ...........................................................................
  /// What happened.
  final AudIoNotificationType type;

  /// The stream it happened to; 0 for the session.
  final int streamId;

  /// A result code, or the permission of a permission notification.
  final int code;

  /// Why it happened.
  final AudIoReason reason;

  /// The format generation of the stream afterwards.
  final int generation;

  /// When it happened, in the host clock.
  final int hostTimeNs;

  /// For [AudIoNotificationType.started], [AudIoNotificationType.recovered]
  /// and [AudIoNotificationType.formatChanged]: the nanoseconds since the
  /// device was lost.
  final int value;

  /// The stream's rate afterwards.
  final double sampleRate;

  /// The stream's output channels afterwards.
  final int outputChannels;

  /// The stream's input channels afterwards.
  final int inputChannels;

  /// [value] as a duration.
  Duration get elapsed => Duration(microseconds: value ~/ 1000);

  /// The permission of a permission notification.
  AudIoPermission get permission => AudIoPermission.fromCode(code);

  @override
  String toString() =>
      'AudIoNotification(${type.name}, stream $streamId, '
      '${reason.name}, generation $generation)';
}
