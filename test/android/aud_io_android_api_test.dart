// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/src/android/aud_io_android_api.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoAndroidDeviceInfo', () {
    test('holds the values of an AudioDeviceInfo', () {
      // Built at run time, not as a constant.
      final id = DateTime.now().year > 0 ? 7 : 0;
      final info = AudIoAndroidDeviceInfo(
        id: id,
        name: 'Pixel',
        type: 2,
        isSink: true,
        isSource: false,
      );
      expect(
        [info.id, info.name, info.type, info.isSink, info.isSource],
        [7, 'Pixel', 2, true, false],
      );
      expect(info.channelCounts, isEmpty);
      expect(info.sampleRates, isEmpty);
    });
  });

  group('AudIoAndroid', () {
    test('names the constants of AudioManager', () {
      expect(
        [
          AudIoAndroid.focusGain,
          AudIoAndroid.focusLoss,
          AudIoAndroid.focusLossTransient,
          AudIoAndroid.focusLossTransientCanDuck,
          AudIoAndroid.focusRequestFailed,
          AudIoAndroid.focusRequestGranted,
          AudIoAndroid.focusRequestDelayed,
        ],
        [1, -1, -2, -3, 0, 1, 2],
      );
    });
  });
}
