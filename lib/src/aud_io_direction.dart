// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_audio_io_bindings_generated.dart' as bindings;

// #############################################################################
/// The directions of a device or a stream.
enum AudIoDirection {
  /// Plays.
  output(bindings.AUD_IO_OUTPUT),

  /// Records.
  input(bindings.AUD_IO_INPUT),

  /// Plays and records.
  duplex(bindings.AUD_IO_DUPLEX);

  const AudIoDirection(this.code);

  /// The `AUD_IO_*` direction code of the C API.
  final int code;

  /// Whether the direction plays.
  bool get hasOutput => (code & bindings.AUD_IO_OUTPUT) != 0;

  /// Whether the direction records.
  bool get hasInput => (code & bindings.AUD_IO_INPUT) != 0;

  // ...........................................................................
  /// The direction with [code].
  static AudIoDirection fromCode(int code) => values.firstWhere(
    (direction) => direction.code == code,
    orElse: () => throw ArgumentError.value(code, 'code', 'Unknown direction'),
  );
}
