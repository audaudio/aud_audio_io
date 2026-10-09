// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// A web entry point of the platform-neutral API: `dart compile js` fails
// when dart:ffi reaches it (web-001).

// ignore_for_file: avoid_print

import 'package:aud_audio_io/aud_audio_io.dart';

void main() {
  print(const AudIoStreamConfig().direction);
  try {
    AudIoSession().dispose();
  } on UnsupportedError catch (e) {
    print(e);
  }
}
