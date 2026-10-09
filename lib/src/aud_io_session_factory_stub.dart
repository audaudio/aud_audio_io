// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.
import 'aud_io_direction.dart';
import 'aud_io_platform.dart';
import 'aud_io_session.dart';

/// The session where the native IO is not available.
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
}) => throw UnsupportedError('The audio IO needs the native engine (web: S5)');
