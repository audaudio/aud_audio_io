// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:test/test.dart';

void main() {
  group('aud_io_constants.dart', () {
    test('matches the ffigen bindings', () async {
      final result = await Process.run('node', [
        'scripts/generate-abi-constants.js',
        'lib/src/aud_audio_io_bindings_generated.dart',
        'lib/src/aud_io_constants.dart',
        '--check',
      ]);
      expect(result.exitCode, 0, reason: '${result.stderr}');
    });

    test('carries the values of the native header', () {
      expect(AUD_IO_BACKEND_NULL, bindings.AUD_IO_BACKEND_NULL);
      expect(AUD_IO_FAULT_FAIL_START, bindings.AUD_IO_FAULT_FAIL_START);
    });
  });
}
