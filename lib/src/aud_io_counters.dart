// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';

import 'aud_io_state.dart';

// #############################################################################
/// The counters of a stream since it opened or since the last reset.
class AudIoCounters {
  /// Creates a snapshot of the counters.
  const AudIoCounters({
    required this.state,
    required this.callbacks,
    required this.frames,
    required this.renders,
    required this.callbackFramesMin,
    required this.callbackFramesMax,
    required this.periodMinNs,
    required this.periodMaxNs,
    required this.periodSumNs,
    required this.periodCount,
    required this.lateCallbacks,
    required this.xruns,
    required this.disconnects,
    required this.recoveries,
    required this.interruptions,
    required this.heldBlocks,
    required this.renderErrors,
    required this.notificationsDropped,
    required this.callbackTimeMaxNs,
    required this.callbackTimeSumNs,
    required this.recoveryTimeMaxNs,
    required this.recoveryTimeLastNs,
    required this.hostTimeJitterMaxNs,
    required this.lastTime,
  });

  // ...........................................................................
  /// The state of the stream.
  final AudIoState state;

  /// Device callbacks.
  final int callbacks;

  /// Frames of the device callbacks.
  final int frames;

  /// Render calls, after splitting the callbacks.
  final int renders;

  /// The smallest device callback.
  final int callbackFramesMin;

  /// The largest device callback.
  final int callbackFramesMax;

  /// The shortest time between the starts of two callbacks.
  final int periodMinNs;

  /// The longest time between the starts of two callbacks.
  final int periodMaxNs;

  /// The sum of the periods.
  final int periodSumNs;

  /// The number of periods summed.
  final int periodCount;

  /// Callbacks that came later than 1.5 times the block before.
  final int lateCallbacks;

  /// Underruns and overruns of the backend.
  final int xruns;

  /// Times the device went away or changed.
  final int disconnects;

  /// Times the device was reopened.
  final int recoveries;

  /// Times the system took the audio.
  final int interruptions;

  /// Blocks played silent while the client acknowledged a new format.
  final int heldBlocks;

  /// Blocks the render function failed.
  final int renderErrors;

  /// Notifications that found the queue full.
  final int notificationsDropped;

  /// The longest time inside the callback.
  final int callbackTimeMaxNs;

  /// The time inside the callback, summed.
  final int callbackTimeSumNs;

  /// The longest time from a loss of the device to the next callback.
  final int recoveryTimeMaxNs;

  /// The time from the last loss of the device to the next callback.
  final int recoveryTimeLastNs;

  /// The largest deviation of a host time from the sample clock.
  final int hostTimeJitterMaxNs;

  /// The time of the last block.
  final AudStreamTime lastTime;

  // ...........................................................................
  /// The mean period between two callbacks.
  double get periodMeanNs => periodCount == 0 ? 0 : periodSumNs / periodCount;

  /// The mean time inside the callback.
  double get callbackTimeMeanNs =>
      callbacks == 0 ? 0 : callbackTimeSumNs / callbacks;
}
