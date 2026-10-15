// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_io_constants.dart' as bindings;

// #############################################################################
/// Where the audio thread of a stream runs (io-003).
enum AudIoPlacement {
  /// The system places the thread: on iOS, on devices with one cluster of
  /// cores below Android 12, before the first callback and after a stop.
  system(bindings.AUD_IO_PLACEMENT_SYSTEM),

  /// Android 12 and later: a performance hint session learns the work of
  /// each callback and moves the thread to faster cores and clocks.
  hint(bindings.AUD_IO_PLACEMENT_HINT),

  /// Below Android 12, or without a hint session: the thread is bound to the
  /// cores above the slowest clock.
  affinity(bindings.AUD_IO_PLACEMENT_AFFINITY),

  /// The system refused the affinity; the thread runs where it places it.
  refused(bindings.AUD_IO_PLACEMENT_REFUSED);

  const AudIoPlacement(this.code);

  /// The `AUD_IO_PLACEMENT_*` code of the C API.
  final int code;

  // ...........................................................................
  /// The placement with [code].
  static AudIoPlacement fromCode(int code) => values.firstWhere(
    (placement) => placement.code == code,
    orElse: () => throw ArgumentError.value(code, 'code', 'Unknown placement'),
  );
}
