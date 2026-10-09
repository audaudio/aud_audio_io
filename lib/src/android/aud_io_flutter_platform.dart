// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Imported where dart:ui exists: in a Flutter app and in flutter test.

import 'dart:async';
import 'dart:io';
import 'dart:ui';

import 'package:flutter/widgets.dart';
import 'package:jni_flutter/jni_flutter.dart';

import '../aud_io_platform.dart';
import 'aud_io_android_jni.dart';
import 'aud_io_android_platform.dart';

// #############################################################################
/// The platform of a session in a Flutter app: Java through JNI on Android,
/// the native backend elsewhere.
AudIoPlatform currentPlatform() {
  if (!Platform.isAndroid) return const AudIoPlatform.native();
  // Android only; the integration test of the example runs it.
  // coverage:ignore-start
  final resumed = StreamController<void>.broadcast();
  WidgetsFlutterBinding.ensureInitialized();
  final lifecycle = AppLifecycleListener(onResume: () => resumed.add(null));
  return AudIoAndroidPlatform(
    AudIoAndroidJni(
      context: androidApplicationContext,
      activity: () {
        final engine = PlatformDispatcher.instance.engineId;
        return engine == null ? null : androidActivity(engine);
      },
    ),
    resumed: resumed.stream,
    onDetach: () {
      lifecycle.dispose();
      unawaited(resumed.close());
    },
  );
  // coverage:ignore-end
}
