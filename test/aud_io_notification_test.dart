// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoNotificationType', () {
    test('fromCode(code) finds every type and refuses others', () {
      expect([
        for (final type in AudIoNotificationType.values)
          AudIoNotificationType.fromCode(type.code),
      ], AudIoNotificationType.values);
      expect(
        () => AudIoNotificationType.fromCode(99),
        throwsA(isA<ArgumentError>()),
      );
    });
  });

  group('AudIoReason', () {
    test('fromCode(code) finds every reason and maps others to none', () {
      expect([
        for (final reason in AudIoReason.values)
          AudIoReason.fromCode(reason.code),
      ], AudIoReason.values);
      expect(AudIoReason.fromCode(99), AudIoReason.none);
    });
  });

  group('AudIoNotification', () {
    test('fromNative(native) reads every field', () {
      final native = calloc<bindings.AudIoNotification>();
      addTearDown(() => calloc.free(native));
      native.ref
        ..type = bindings.AUD_IO_NOTIFY_STARTED
        ..stream = 3
        ..code = -1
        ..reason = bindings.AUD_IO_REASON_SAMPLE_RATE
        ..generation = 2
        ..host_time_ns = 1000
        ..value = 250000000
        ..sample_rate = 44100
        ..output_channels = 2
        ..input_channels = 1;
      final notification = AudIoNotification.fromNative(native.ref);
      expect(notification.type, AudIoNotificationType.started);
      expect(notification.streamId, 3);
      expect(notification.code, -1);
      expect(notification.reason, AudIoReason.sampleRate);
      expect(notification.generation, 2);
      expect(notification.hostTimeNs, 1000);
      expect(notification.elapsed, const Duration(milliseconds: 250));
      expect(notification.sampleRate, 44100);
      expect(notification.outputChannels, 2);
      expect(notification.inputChannels, 1);
      expect(
        notification.toString(),
        'AudIoNotification(started, stream 3, sampleRate, generation 2)',
      );
    });

    test('permission reads the code of a permission notification', () {
      final notification = AudIoNotification(
        type: AudIoNotificationType.permission,
        code: AudIoPermission.denied.code,
      );
      expect(notification.permission, AudIoPermission.denied);
    });
  });
}
