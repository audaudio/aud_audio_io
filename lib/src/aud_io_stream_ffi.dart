// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';
import 'dart:typed_data';

import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:ffi/ffi.dart';

import 'aud_audio_io_bindings_generated.dart' as bindings;
import 'aud_io_counters.dart';
import 'aud_io_exception.dart';
import 'aud_io_notification.dart';
import 'aud_io_native_conversions.dart';
import 'aud_io_session_ffi.dart';
import 'aud_io_stream.dart';
import 'aud_io_state.dart';
import 'aud_io_stream_config.dart';
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
class AudIoStreamFfi implements AudIoStream {
  /// Opens a stream of [session] in the stopped state.
  ///
  /// - [config] what the stream asks for
  /// - [render] the native render function, called on the audio thread
  /// - [user] handed to [render], e.g. the native graph
  AudIoStreamFfi.open(
    this.session,
    AudIoStreamConfig config, {
    required core.AudRenderFunction render,
    Pointer<Void>? user,
  }) {
    if (session.isDisposed) {
      throw const AudIoException(
        core.AUD_ERROR_STATE,
        'The session is disposed.',
      );
    }
    _pointer = using((arena) {
      final native = config.toNative(arena, render: render, user: user);
      final result = arena<Int32>();
      final pointer = bindings.aud_io_stream_open(
        session.pointer,
        native,
        result,
      );
      AudIoException.check(result.value, 'open the stream');
      return pointer;
    });
    id = bindings.aud_io_stream_id(_pointer);
    session.adopt(this);
  }

  // ...........................................................................
  /// A render function that plays a 440 Hz sine at -20 dBFS: the smoke
  /// signal of the package.
  static core.AudRenderFunction get sineRender =>
      Native.addressOf<NativeFunction<core.AudRenderFunctionFunction>>(
        bindings.aud_io_sine_render,
      );

  /// A render function that copies the input into the output: a monitor.
  static core.AudRenderFunction get thruRender =>
      Native.addressOf<NativeFunction<core.AudRenderFunctionFunction>>(
        bindings.aud_io_thru_render,
      );

  // ...........................................................................
  /// The session of the stream.
  @override
  final AudIoSessionFfi session;

  /// The id that names the stream in notifications.
  @override
  late final int id;

  /// The native stream.
  Pointer<bindings.AudIoStream> get pointer => _pointer;

  /// Whether [close] ran.
  @override
  bool get isClosed => _pointer == nullptr;

  /// The state of the stream.
  @override
  AudIoState get state {
    _checkOpen();
    return AudIoState.fromCode(
      AudIoException.check(
        bindings.aud_io_stream_state(_pointer),
        'read the state',
      ),
    );
  }

  /// What the stream got from its device.
  @override
  AudIoStreamFormat get format {
    _checkOpen();
    final native = calloc<bindings.AudIoStreamFormat>();
    try {
      native.ref.struct_size = sizeOf<bindings.AudIoStreamFormat>();
      AudIoException.check(
        bindings.aud_io_stream_format(_pointer, native),
        'read the format',
      );
      return native.ref.toDart();
    } finally {
      calloc.free(native);
    }
  }

  /// The counters since the stream opened or since [resetCounters].
  @override
  AudIoCounters get counters {
    _checkOpen();
    final native = calloc<bindings.AudIoCounters>();
    try {
      native.ref.struct_size = sizeOf<bindings.AudIoCounters>();
      AudIoException.check(
        bindings.aud_io_stream_counters(_pointer, native),
        'read the counters',
      );
      return native.ref.toDart();
    } finally {
      calloc.free(native);
    }
  }

  /// The notifications of this stream.
  @override
  Stream<AudIoNotification> get notifications =>
      session.notifications.where((n) => n.streamId == id);

  // ...........................................................................
  /// Starts the callbacks; from [AudIoState.failed] it reopens first, during
  /// an interruption it starts when the interruption ends.
  @override
  void start() {
    _checkOpen();
    AudIoException.check(
      bindings.aud_io_stream_start(_pointer),
      'start the stream',
    );
  }

  /// Stops the callbacks; returns when no callback runs.
  @override
  void stop() {
    _checkOpen();
    AudIoException.check(
      bindings.aud_io_stream_stop(_pointer),
      'stop the stream',
    );
  }

  /// Tells the stream that the render function is prepared for the format
  /// of [generation]; it renders again from the next block.
  @override
  void acknowledge(int generation) {
    _checkOpen();
    AudIoException.check(
      bindings.aud_io_stream_acknowledge(_pointer, generation),
      'acknowledge generation $generation',
    );
  }

  /// Zeroes the counters.
  @override
  void resetCounters() {
    _checkOpen();
    AudIoException.check(
      bindings.aud_io_stream_reset_counters(_pointer),
      'reset the counters',
    );
  }

  /// Stops and closes the stream; no callback runs afterwards. Can be
  /// called twice.
  @override
  void close() {
    if (isClosed) return;
    bindings.aud_io_stream_close(_pointer);
    _pointer = nullptr;
    session.release(this);
  }

  // ...........................................................................
  /// Runs one callback of a null device with the manual clock: [frames]
  /// frames at [hostTimeNs] with the interleaved [input], or the loop of
  /// the null device without one; returns the interleaved output.
  @override
  Float32List debugProcess({
    required int frames,
    required int hostTimeNs,
    Float32List? input,
  }) {
    _checkOpen();
    final channels = format.outputChannels;
    final output = calloc<Float>(frames * channels + 1);
    final nativeInput = input == null
        ? nullptr
        : calloc<Float>(input.length + 1);
    try {
      if (input != null) nativeInput.asTypedList(input.length).setAll(0, input);
      AudIoException.check(
        bindings.aud_io_null_process(
          _pointer,
          nativeInput,
          channels == 0 ? nullptr : output,
          frames,
          hostTimeNs,
        ),
        'run a callback',
      );
      return Float32List.fromList(output.asTypedList(frames * channels));
    } finally {
      calloc.free(output);
      if (nativeInput != nullptr) calloc.free(nativeInput);
    }
  }

  // ...........................................................................
  late Pointer<bindings.AudIoStream> _pointer;

  void _checkOpen() {
    if (isClosed) {
      throw const AudIoException(core.AUD_ERROR_STATE, 'The stream is closed.');
    }
  }
}
