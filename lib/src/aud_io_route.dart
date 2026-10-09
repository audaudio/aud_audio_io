// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_io_constants.dart' as bindings;

// #############################################################################
/// The kind of route a device belongs to.
enum AudIoRoute {
  /// A route the backend does not name.
  unknown(bindings.AUD_IO_ROUTE_UNKNOWN),

  /// The built-in speaker.
  speaker(bindings.AUD_IO_ROUTE_SPEAKER),

  /// The earpiece.
  receiver(bindings.AUD_IO_ROUTE_RECEIVER),

  /// The built-in microphone.
  builtinMic(bindings.AUD_IO_ROUTE_BUILTIN_MIC),

  /// Wired headphones.
  wiredHeadphones(bindings.AUD_IO_ROUTE_WIRED_HEADPHONES),

  /// Wired headphones with a microphone.
  wiredHeadset(bindings.AUD_IO_ROUTE_WIRED_HEADSET),

  /// A line connection.
  line(bindings.AUD_IO_ROUTE_LINE),

  /// A USB audio device.
  usb(bindings.AUD_IO_ROUTE_USB),

  /// Bluetooth with the A2DP profile: output only, high quality.
  bluetoothA2dp(bindings.AUD_IO_ROUTE_BLUETOOTH_A2DP),

  /// Bluetooth hands-free: input and output at speech quality.
  bluetoothHfp(bindings.AUD_IO_ROUTE_BLUETOOTH_HFP),

  /// Bluetooth Low Energy audio.
  bluetoothLe(bindings.AUD_IO_ROUTE_BLUETOOTH_LE),

  /// HDMI.
  hdmi(bindings.AUD_IO_ROUTE_HDMI),

  /// AirPlay.
  airplay(bindings.AUD_IO_ROUTE_AIRPLAY),

  /// A car's audio system.
  car(bindings.AUD_IO_ROUTE_CAR),

  /// A hearing aid.
  hearingAid(bindings.AUD_IO_ROUTE_HEARING_AID),

  /// The null device.
  virtual(bindings.AUD_IO_ROUTE_VIRTUAL);

  const AudIoRoute(this.code);

  /// The `AUD_IO_ROUTE_*` code of the C API.
  final int code;

  // ...........................................................................
  /// The route with [code]; [unknown] for a code a newer backend reports.
  static AudIoRoute fromCode(int code) =>
      values.firstWhere((route) => route.code == code, orElse: () => unknown);
}
