// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.
import 'aud_io_direction.dart';
import 'aud_io_platform.dart';
import 'aud_io_session.dart';
import 'aud_io_session_ffi.dart';

/// A native [AudIoSession]; see its constructor.
AudIoSession createSession({
  required AudIoBackend backend,
  required AudIoDirection directions,
  required bool mixWithOthers,
  required bool bluetoothHfp,
  required bool measurement,
  required bool manualClock,
  required int notificationCapacity,
  required Duration recoveryTimeout,
  required bool listen,
  required AudIoPlatform? platform,
}) => AudIoSessionFfi(
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
