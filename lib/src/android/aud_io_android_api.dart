// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// An `AudioDeviceInfo` of Android as plain values.
class AudIoAndroidDeviceInfo {
  /// Creates the values of a device.
  const AudIoAndroidDeviceInfo({
    required this.id,
    required this.name,
    required this.type,
    required this.isSink,
    required this.isSource,
    this.channelCounts = const [],
    this.sampleRates = const [],
  });

  /// The id Oboe opens the device with.
  final int id;

  /// The product name.
  final String name;

  /// One of the `TYPE_*` constants of `AudioDeviceInfo`.
  final int type;

  /// Whether the device plays.
  final bool isSink;

  /// Whether the device records.
  final bool isSource;

  /// The channel counts it supports; empty: any.
  final List<int> channelCounts;

  /// The sample rates it supports; empty: any.
  final List<int> sampleRates;
}

// #############################################################################
/// What the Android platform asks of Java: the audio manager, the audio
/// focus and the microphone permission. The JNI implementation runs on
/// Android; tests hand in their own.
abstract interface class AudIoAndroidApi {
  /// The SDK level of the device.
  int get sdkLevel;

  /// The devices `AudioManager.getDevices` lists.
  List<AudIoAndroidDeviceInfo> devices();

  /// The ids of the devices media plays on now; empty below API 33.
  List<int> mediaDeviceIds();

  /// Whether the app holds the microphone permission.
  bool get hasRecordPermission;

  /// Shows the dialog that asks for the microphone permission.
  void requestRecordPermission();

  /// Asks for the audio focus for music; [onChange] receives the
  /// `AUDIOFOCUS_*` changes. Returns an `AUDIOFOCUS_REQUEST_*` result.
  int requestFocus(void Function(int change) onChange);

  /// Gives the audio focus back.
  void abandonFocus();
}

// #############################################################################
/// The constants of Android the platform uses.
abstract final class AudIoAndroid {
  /// `AudioManager.AUDIOFOCUS_GAIN`.
  static const focusGain = 1;

  /// `AudioManager.AUDIOFOCUS_LOSS`.
  static const focusLoss = -1;

  /// `AudioManager.AUDIOFOCUS_LOSS_TRANSIENT`.
  static const focusLossTransient = -2;

  /// `AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK`.
  static const focusLossTransientCanDuck = -3;

  /// `AudioManager.AUDIOFOCUS_REQUEST_FAILED`.
  static const focusRequestFailed = 0;

  /// `AudioManager.AUDIOFOCUS_REQUEST_GRANTED`.
  static const focusRequestGranted = 1;

  /// `AudioManager.AUDIOFOCUS_REQUEST_DELAYED`.
  static const focusRequestDelayed = 2;
}
