// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:typed_data';

import 'aud_io_counters.dart';
import 'aud_io_notification.dart';
import 'aud_io_session.dart';
import 'aud_io_state.dart';
import 'aud_io_stream_format.dart';

// #############################################################################
/// A stream of a session: plays and records on its devices and calls a
/// native render function from its audio thread with planar blocks of at
/// most [AudIoStreamFormat.maxFrames] frames, each with its stream time.
///
/// The render function is native, e.g. `aud_graph_render` of
/// aud_audio_graph with the graph's pointer as user:
///
/// ```dart
/// final stream = session.open(
///   AudIoStreamConfig(maxFrames: graph.maxFrames),
///   render: Native.addressOf(graphBindings.aud_graph_render),
///   user: graph.pointer.cast(),
/// );
/// ```
///
/// The stream recovers from losses and changes of its device on its own and
/// reports each case in [notifications]. After
/// [AudIoNotificationType.formatChanged] it plays silence until the client
/// prepared its renderer for the new [format] and called [acknowledge].
///
/// The platform-neutral API: on native platforms `AudIoSessionFfi.open`
/// opens an `AudIoStreamFfi` with a render function.
abstract interface class AudIoStream {
  /// The session of the stream.
  AudIoSession get session;

  /// The id that names the stream in notifications.
  int get id;

  /// Whether [close] ran.
  bool get isClosed;

  /// The state of the stream.
  AudIoState get state;

  /// What the stream got from its device.
  AudIoStreamFormat get format;

  /// The counters since the stream opened or since [resetCounters].
  AudIoCounters get counters;

  /// The notifications of this stream.
  Stream<AudIoNotification> get notifications;

  /// Starts the callbacks; from [AudIoState.failed] it reopens first, during
  /// an interruption it starts when the interruption ends.
  void start();

  /// Stops the callbacks; returns when no callback runs.
  void stop();

  /// Tells the stream that the render function is prepared for the format
  /// of [generation]; it renders again from the next block.
  void acknowledge(int generation);

  /// Zeroes the counters.
  void resetCounters();

  /// Stops and closes the stream; no callback runs afterwards. Can be
  /// called twice.
  void close();

  /// Runs one callback of a null device with the manual clock: [frames]
  /// frames at [hostTimeNs] with the interleaved [input], or the loop of
  /// the null device without one; returns the interleaved output.
  Float32List debugProcess({
    required int frames,
    required int hostTimeNs,
    Float32List? input,
  });
}
