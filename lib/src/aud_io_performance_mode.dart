// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_io_constants.dart' as bindings;

// #############################################################################
/// What a stream optimizes for.
enum AudIoPerformanceMode {
  /// The lowest latency the device offers.
  lowLatency(bindings.AUD_IO_PERFORMANCE_LOW_LATENCY),

  /// The backend's default.
  none(bindings.AUD_IO_PERFORMANCE_NONE),

  /// Large buffers that save power.
  powerSaving(bindings.AUD_IO_PERFORMANCE_POWER_SAVING);

  const AudIoPerformanceMode(this.code);

  /// The `AUD_IO_PERFORMANCE_*` code of the C API.
  final int code;

  // ...........................................................................
  /// The mode with [code].
  static AudIoPerformanceMode fromCode(int code) => values.firstWhere(
    (mode) => mode.code == code,
    orElse: () => throw ArgumentError.value(code, 'code', 'Unknown mode'),
  );
}
