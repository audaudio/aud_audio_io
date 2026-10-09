// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_audio_io_bindings_generated.dart' as bindings;

// #############################################################################
/// The states of a stream.
enum AudIoState {
  /// Opened or stopped: no callbacks.
  stopped(bindings.AUD_IO_STATE_STOPPED),

  /// The callbacks run.
  running(bindings.AUD_IO_STATE_RUNNING),

  /// The stream reopens its device after a loss or a change.
  recovering(bindings.AUD_IO_STATE_RECOVERING),

  /// The system holds the audio; the stream resumes when it is given back.
  interrupted(bindings.AUD_IO_STATE_INTERRUPTED),

  /// The recovery gave up; starting the stream tries again.
  failed(bindings.AUD_IO_STATE_FAILED);

  const AudIoState(this.code);

  /// The `AUD_IO_STATE_*` code of the C API.
  final int code;

  // ...........................................................................
  /// The state with [code].
  static AudIoState fromCode(int code) => values.firstWhere(
    (state) => state.code == code,
    orElse: () => throw ArgumentError.value(code, 'code', 'Unknown state'),
  );
}
