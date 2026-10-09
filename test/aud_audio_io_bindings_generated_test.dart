// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:test/test.dart';

void main() {
  group('aud_audio_io_bindings_generated.dart', () {
    test('the C API refuses null handles', () {
      expect(bindings.aud_io_session_create(nullptr), nullptr);
      expect(bindings.aud_io_stream_open(nullptr, nullptr, nullptr), nullptr);
      expect(bindings.aud_io_stream_start(nullptr), AUD_ERROR_INVALID_ARGUMENT);
      expect(bindings.aud_io_stream_stop(nullptr), AUD_ERROR_INVALID_ARGUMENT);
      expect(bindings.aud_io_stream_state(nullptr), AUD_ERROR_INVALID_ARGUMENT);
      expect(
        bindings.aud_io_session_take_notifications(nullptr, nullptr, 0),
        AUD_ERROR_INVALID_ARGUMENT,
      );
      bindings.aud_io_stream_close(nullptr);
      bindings.aud_io_session_destroy(nullptr);
    });

    test('the API version is the one the Dart side was written for', () {
      expect(bindings.AUD_IO_API_VERSION, 2);
    });
  });
}
