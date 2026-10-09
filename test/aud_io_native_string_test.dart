// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoNativeString', () {
    test('write(chars, capacity, value) and read(chars, capacity)', () {
      final device = calloc<bindings.AudIoDevice>();
      addTearDown(() => calloc.free(device));
      AudIoNativeString.write(device.ref.name, 128, 'Kopfhörer');
      expect(AudIoNativeString.read(device.ref.name, 128), 'Kopfhörer');
      // Cut so that the terminating NUL fits.
      AudIoNativeString.write(device.ref.name, 4, 'abcdef');
      expect(AudIoNativeString.read(device.ref.name, 128), 'abc');
      // Without a NUL the capacity ends the string.
      for (var i = 0; i < 8; i++) {
        device.ref.id[i] = 0x41;
      }
      expect(AudIoNativeString.read(device.ref.id, 8), 'AAAAAAAA');
    });
  });
}
