// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:convert';
import 'dart:ffi';

// #############################################################################
/// The fixed strings of the C structs: a NUL-terminated UTF-8 array of
/// `char` of a known capacity.
abstract final class AudIoNativeString {
  /// Reads the string in [chars], at most [capacity] bytes.
  static String read(Array<Char> chars, int capacity) {
    final bytes = <int>[];
    for (var i = 0; i < capacity; i++) {
      final byte = chars[i] & 0xff;
      if (byte == 0) break;
      bytes.add(byte);
    }
    return utf8.decode(bytes, allowMalformed: true);
  }

  /// Writes [value] into [chars] of [capacity] bytes, cut so that the
  /// terminating NUL fits.
  static void write(Array<Char> chars, int capacity, String value) {
    final bytes = utf8.encode(value);
    final length = bytes.length < capacity ? bytes.length : capacity - 1;
    for (var i = 0; i < length; i++) {
      chars[i] = bytes[i];
    }
    chars[length] = 0;
  }
}
