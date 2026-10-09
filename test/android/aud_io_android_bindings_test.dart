// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:aud_audio_io/src/android/aud_io_android_api.dart';
import 'package:aud_audio_io/src/android/aud_io_android_bindings.dart';
import 'package:aud_audio_io/src/android/aud_io_android_platform.dart';
import 'package:test/test.dart';

// The constants of the bindings need no JVM; the calls run on Android, in
// the integration test of the example.
void main() {
  group('aud_io_android_bindings.dart', () {
    test('the focus constants are the ones the platform uses', () {
      expect(
        [
          AudioManager.AUDIOFOCUS_GAIN,
          AudioManager.AUDIOFOCUS_LOSS,
          AudioManager.AUDIOFOCUS_LOSS_TRANSIENT,
          AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK,
          AudioManager.AUDIOFOCUS_REQUEST_GRANTED,
          AudioManager.AUDIOFOCUS_REQUEST_DELAYED,
        ],
        [
          AudIoAndroid.focusGain,
          AudIoAndroid.focusLoss,
          AudIoAndroid.focusLossTransient,
          AudIoAndroid.focusLossTransientCanDuck,
          AudIoAndroid.focusRequestGranted,
          AudIoAndroid.focusRequestDelayed,
        ],
      );
    });

    test('routeOf(type) knows the device types of Android', () {
      expect(
        {
          for (final (type, route) in [
            (AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, AudIoRoute.receiver),
            (AudioDeviceInfo.TYPE_BUILTIN_SPEAKER, AudIoRoute.speaker),
            (AudioDeviceInfo.TYPE_BUILTIN_SPEAKER_SAFE, AudIoRoute.speaker),
            (AudioDeviceInfo.TYPE_WIRED_HEADSET, AudIoRoute.wiredHeadset),
            (AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudIoRoute.wiredHeadphones),
            (AudioDeviceInfo.TYPE_LINE_ANALOG, AudIoRoute.line),
            (AudioDeviceInfo.TYPE_LINE_DIGITAL, AudIoRoute.line),
            (AudioDeviceInfo.TYPE_DOCK, AudIoRoute.line),
            (AudioDeviceInfo.TYPE_AUX_LINE, AudIoRoute.line),
            (AudioDeviceInfo.TYPE_DOCK_ANALOG, AudIoRoute.line),
            (AudioDeviceInfo.TYPE_BLUETOOTH_SCO, AudIoRoute.bluetoothHfp),
            (AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, AudIoRoute.bluetoothA2dp),
            (AudioDeviceInfo.TYPE_HDMI, AudIoRoute.hdmi),
            (AudioDeviceInfo.TYPE_HDMI_ARC, AudIoRoute.hdmi),
            (AudioDeviceInfo.TYPE_HDMI_EARC, AudIoRoute.hdmi),
            (AudioDeviceInfo.TYPE_USB_DEVICE, AudIoRoute.usb),
            (AudioDeviceInfo.TYPE_USB_ACCESSORY, AudIoRoute.usb),
            (AudioDeviceInfo.TYPE_USB_HEADSET, AudIoRoute.usb),
            (AudioDeviceInfo.TYPE_BUILTIN_MIC, AudIoRoute.builtinMic),
            (AudioDeviceInfo.TYPE_BUS, AudIoRoute.car),
            (AudioDeviceInfo.TYPE_HEARING_AID, AudIoRoute.hearingAid),
            (AudioDeviceInfo.TYPE_BLE_HEADSET, AudIoRoute.bluetoothLe),
            (AudioDeviceInfo.TYPE_BLE_SPEAKER, AudIoRoute.bluetoothLe),
            (AudioDeviceInfo.TYPE_BLE_BROADCAST, AudIoRoute.bluetoothLe),
            (AudioDeviceInfo.TYPE_TELEPHONY, AudIoRoute.unknown),
          ])
            type: AudIoAndroidPlatform.routeOf(type) == route,
        }.values,
        everyElement(isTrue),
      );
    });

    test('the attributes are the ones of music', () {
      expect(AudioAttributes.USAGE_MEDIA, 1);
      expect(AudioAttributes.CONTENT_TYPE_MUSIC, 2);
      expect(AudioManager.GET_DEVICES_ALL, 3);
    });
  });
}
