// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/src/android/aud_io_android_api.dart';
import 'package:aud_audio_io/src/android/aud_io_android_jni.dart';
import 'package:jni/jni.dart';
import 'package:test/test.dart';

// AudIoAndroidJni calls Java and needs Android's JVM: the integration test
// of the example runs it on the emulator and the devices, the logic it
// serves is tested on the host in aud_io_android_platform_test.dart. Here
// the host proves that it compiles against the bindings and serves the API.
void main() {
  group('AudIoAndroidJni', () {
    test('is the AudIoAndroidApi of Android', () {
      AudIoAndroidApi create(JObject context) =>
          AudIoAndroidJni(context: context);
      expect(create, isA<AudIoAndroidApi Function(JObject)>());
    });
  });
}
