// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';

import 'aud_io_constants.dart' as bindings;

// #############################################################################
/// Thrown when a session or a stream refuses a call.
class AudIoException implements Exception {
  /// Creates the exception for a result [code] and a [message].
  const AudIoException(this.code, this.message);

  /// The result code: an `AUD_ERROR_*` of the ABI or an `AUD_IO_ERROR_*`.
  final int code;

  /// What failed.
  final String message;

  /// The name of [code], e.g. `AUD_IO_ERROR_PERMISSION`.
  String get codeName => resultName(code);

  @override
  String toString() => 'AudIoException($codeName): $message';

  // ...........................................................................
  /// The name of a result code of the ABI or of aud_audio_io.
  static String resultName(int code) => switch (code) {
    bindings.AUD_IO_ERROR_PERMISSION => 'AUD_IO_ERROR_PERMISSION',
    bindings.AUD_IO_ERROR_NO_DEVICE => 'AUD_IO_ERROR_NO_DEVICE',
    bindings.AUD_IO_ERROR_DEVICE => 'AUD_IO_ERROR_DEVICE',
    bindings.AUD_IO_ERROR_INTERRUPTED => 'AUD_IO_ERROR_INTERRUPTED',
    _ => AudAbi.resultName(code),
  };

  /// Returns [result], or throws it as an exception when it is an error.
  ///
  /// - [result] what a native call returned
  /// - [what] the call, e.g. `start the stream`
  static int check(int result, String what) {
    if (result < 0) {
      throw AudIoException(result, 'Could not $what: ${resultName(result)}');
    }
    return result;
  }
}
