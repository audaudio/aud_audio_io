// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_io_device.dart';
import 'aud_io_permission.dart';
import 'aud_io_session.dart';

// #############################################################################
/// What a platform adds to the native backend of a session. On Android the
/// devices, the audio focus and the microphone permission live in Java; a
/// platform reaches them from Dart (ticket 21, decision 1). Everywhere else
/// the native backend answers and [AudIoPlatform.native] adds nothing.
class AudIoPlatform {
  /// The platform that leaves everything to the native backend.
  const AudIoPlatform.native();

  /// The devices; null lets the native backend list them.
  List<AudIoDevice>? devices() => null;

  /// The microphone permission; null lets the native backend answer.
  AudIoPermission? permission() => null;

  /// Asks the user for the microphone permission; null lets the native
  /// backend ask.
  Future<AudIoPermission>? requestPermission() => null;

  /// Starts following the platform for [session]: device changes reach
  /// [AudIoSession.reportDevicesChanged], the loss and the return of the
  /// audio focus [AudIoSession.interrupt] and [AudIoSession.resume].
  void attach(AudIoSession session) {}

  /// Stops following the platform.
  void detach() {}
}
