// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_io_direction.dart';
import 'aud_io_route.dart';

// #############################################################################
/// An audio device: a speaker, a microphone, a headset, an interface.
class AudIoDevice {
  /// Creates a device.
  ///
  /// - [id] stable while the device is attached: the device id of
  ///   AudioManager on Android, the port UID on iOS
  /// - [name] the name to show
  /// - [directions] whether it plays, records or both
  /// - [route] the kind of route
  /// - [isDefaultOutput] the output a stream takes without an id
  /// - [isDefaultInput] the input a stream takes without an id
  /// - [isActive] part of the current route (iOS) or in use
  /// - [maxOutputChannels] the most output channels
  /// - [maxInputChannels] the most input channels
  /// - [sampleRates] the rates it runs at; empty: any rate the backend
  ///   converts to
  const AudIoDevice({
    required this.id,
    required this.name,
    required this.directions,
    this.route = AudIoRoute.unknown,
    this.isDefaultOutput = false,
    this.isDefaultInput = false,
    this.isActive = false,
    this.maxOutputChannels = 0,
    this.maxInputChannels = 0,
    this.sampleRates = const [],
  });

  // ...........................................................................
  /// Stable while the device is attached.
  final String id;

  /// The name to show.
  final String name;

  /// Whether the device plays, records or both.
  final AudIoDirection directions;

  /// The kind of route.
  final AudIoRoute route;

  /// The output a stream takes without an id.
  final bool isDefaultOutput;

  /// The input a stream takes without an id.
  final bool isDefaultInput;

  /// Part of the current route (iOS) or in use.
  final bool isActive;

  /// The most output channels.
  final int maxOutputChannels;

  /// The most input channels.
  final int maxInputChannels;

  /// The rates the device runs at; empty: any rate the backend converts to.
  final List<double> sampleRates;

  // ...........................................................................
  @override
  bool operator ==(Object other) =>
      other is AudIoDevice &&
      other.id == id &&
      other.name == name &&
      other.directions == directions &&
      other.route == route &&
      other.isDefaultOutput == isDefaultOutput &&
      other.isDefaultInput == isDefaultInput &&
      other.isActive == isActive &&
      other.maxOutputChannels == maxOutputChannels &&
      other.maxInputChannels == maxInputChannels &&
      _sameRates(other.sampleRates, sampleRates);

  @override
  int get hashCode => Object.hash(
    id,
    name,
    directions,
    route,
    isDefaultOutput,
    isDefaultInput,
    isActive,
    maxOutputChannels,
    maxInputChannels,
    Object.hashAll(sampleRates),
  );

  @override
  String toString() =>
      'AudIoDevice($id, $name, ${directions.name}, '
      '${route.name})';

  static bool _sameRates(List<double> a, List<double> b) {
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}
