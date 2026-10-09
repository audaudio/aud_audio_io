// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_io_constants.dart' as bindings;

// #############################################################################
/// The microphone permission.
enum AudIoPermission {
  /// The user was not asked yet.
  undetermined(bindings.AUD_IO_PERMISSION_UNDETERMINED),

  /// The user refused it.
  denied(bindings.AUD_IO_PERMISSION_DENIED),

  /// The user granted it.
  granted(bindings.AUD_IO_PERMISSION_GRANTED);

  const AudIoPermission(this.code);

  /// The `AUD_IO_PERMISSION_*` code of the C API.
  final int code;

  // ...........................................................................
  /// The permission with [code].
  static AudIoPermission fromCode(int code) => values.firstWhere(
    (permission) => permission.code == code,
    orElse: () => throw ArgumentError.value(code, 'code', 'Unknown permission'),
  );
}
