// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core_ffi.dart';
import 'package:aud_audio_io/aud_audio_io_ffi.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudIoDevice', () {
    test('toDart() reads every field', () {
      final native = calloc<bindings.AudIoDevice>();
      addTearDown(() => calloc.free(native));
      native.ref
        ..struct_size = 1
        ..directions = bindings.AUD_IO_DUPLEX
        ..route = bindings.AUD_IO_ROUTE_USB
        ..flags =
            bindings.AUD_IO_DEVICE_DEFAULT_OUTPUT |
            bindings.AUD_IO_DEVICE_DEFAULT_INPUT |
            bindings.AUD_IO_DEVICE_ACTIVE
        ..max_output_channels = 8
        ..max_input_channels = 6
        ..num_sample_rates = 2;
      native.ref.sample_rates[0] = 44100;
      native.ref.sample_rates[1] = 48000;
      AudIoNativeString.write(native.ref.id, bindings.AUD_IO_MAX_ID, '17');
      AudIoNativeString.write(native.ref.name, bindings.AUD_IO_MAX_NAME, 'UMC');
      final device = native.ref.toDart();
      expect(
        device,
        const AudIoDevice(
          id: '17',
          name: 'UMC',
          directions: AudIoDirection.duplex,
          route: AudIoRoute.usb,
          isDefaultOutput: true,
          isDefaultInput: true,
          isActive: true,
          maxOutputChannels: 8,
          maxInputChannels: 6,
          sampleRates: [44100, 48000],
        ),
      );
      expect(device.toString(), 'AudIoDevice(17, UMC, duplex, usb)');
    });
  });

  group('AudIoCounters', () {
    test('toDart() reads every field', () {
      final native = calloc<bindings.AudIoCounters>();
      addTearDown(() => calloc.free(native));
      var value = 1;
      native.ref
        ..state = bindings.AUD_IO_STATE_RECOVERING
        ..callbacks = 10
        ..frames = value++
        ..renders = value++
        ..callback_frames_min = value++
        ..callback_frames_max = value++
        ..period_min_ns = value++
        ..period_max_ns = value++
        ..period_sum_ns = 900
        ..period_count = 9
        ..late_callbacks = value++
        ..xruns = value++
        ..disconnects = value++
        ..recoveries = value++
        ..interruptions = value++
        ..held_blocks = value++
        ..render_errors = value++
        ..notifications_dropped = value++
        ..callback_time_max_ns = value++
        ..callback_time_sum_ns = 50
        ..recovery_time_max_ns = value++
        ..recovery_time_last_ns = value++
        ..host_time_jitter_max_ns = value++;
      native.ref.last_time
        ..frames = 64
        ..sample_rate = 48000
        ..sample_position = 128
        ..host_time_source = AUD_TIME_SOURCE_ESTIMATED;
      final counters = native.ref.toDart();
      expect(counters.state, AudIoState.recovering);
      expect(
        [
          counters.frames,
          counters.renders,
          counters.callbackFramesMin,
          counters.callbackFramesMax,
          counters.periodMinNs,
          counters.periodMaxNs,
          counters.lateCallbacks,
          counters.xruns,
          counters.disconnects,
          counters.recoveries,
          counters.interruptions,
          counters.heldBlocks,
          counters.renderErrors,
          counters.notificationsDropped,
          counters.callbackTimeMaxNs,
          counters.recoveryTimeMaxNs,
          counters.recoveryTimeLastNs,
          counters.hostTimeJitterMaxNs,
        ],
        [for (var i = 1; i <= 18; i++) i],
      );
      expect(counters.callbacks, 10);
      expect(counters.periodSumNs, 900);
      expect(counters.periodCount, 9);
      expect(counters.periodMeanNs, 100);
      expect(counters.callbackTimeSumNs, 50);
      expect(counters.callbackTimeMeanNs, 5);
      expect(counters.lastTime.frames, 64);
      expect(counters.lastTime.samplePosition, 128);
      expect(counters.lastTime.hostTimeSource, AudTimeSource.estimated);
    });

    test('the means are 0 without callbacks', () {
      final native = calloc<bindings.AudIoCounters>();
      addTearDown(() => calloc.free(native));
      final counters = native.ref.toDart();
      expect(counters.periodMeanNs, 0);
      expect(counters.callbackTimeMeanNs, 0);
    });
  });

  group('AudIoNotification', () {
    test('toDart() reads every field', () {
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
      final notification = native.ref.toDart();
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
  });

  group('AudIoStreamConfig', () {
    test('toNative(arena, render, user) writes every field', () {
      using((arena) {
        const config = AudIoStreamConfig(
          direction: AudIoDirection.duplex,
          outputDeviceId: '17',
          inputDeviceId: '18',
          outputChannels: 4,
          inputChannels: 2,
          sampleRate: 48000,
          bufferFrames: 96,
          maxFrames: 512,
          performanceMode: AudIoPerformanceMode.powerSaving,
          followFormat: true,
        );
        final native = config
            .toNative(
              arena,
              render: AudIoStreamFfi.sineRender,
              user: Pointer.fromAddress(8),
            )
            .ref;
        expect(native.struct_size, sizeOf<bindings.AudIoStreamConfig>());
        expect(native.direction, bindings.AUD_IO_DUPLEX);
        expect(native.output_device_id.cast<Utf8>().toDartString(), '17');
        expect(native.input_device_id.cast<Utf8>().toDartString(), '18');
        expect(native.output_channels, 4);
        expect(native.input_channels, 2);
        expect(native.sample_rate, 48000);
        expect(native.buffer_frames, 96);
        expect(native.max_frames, 512);
        expect(
          native.performance_mode,
          bindings.AUD_IO_PERFORMANCE_POWER_SAVING,
        );
        expect(
          native.flags,
          bindings.AUD_IO_STREAM_EXCLUSIVE |
              bindings.AUD_IO_STREAM_LATENCY_TUNER |
              bindings.AUD_IO_STREAM_FOLLOW_FORMAT,
        );
        expect(native.render, AudIoStreamFfi.sineRender);
        expect(native.render_user.address, 8);
      });
    });

    test('leaves the defaults at 0 and null', () {
      using((arena) {
        final native = const AudIoStreamConfig(
          exclusive: false,
          latencyTuner: false,
        ).toNative(arena, render: AudIoStreamFfi.sineRender).ref;
        expect(native.direction, bindings.AUD_IO_OUTPUT);
        expect(native.output_device_id, nullptr);
        expect(native.input_device_id, nullptr);
        expect(native.output_channels, 0);
        expect(native.sample_rate, 0);
        expect(native.max_frames, 0);
        expect(native.flags, 0);
        expect(native.render_user, nullptr);
      });
    });
  });

  group('AudIoStreamFormat', () {
    test('toDart() reads every field', () {
      final native = calloc<bindings.AudIoStreamFormat>();
      addTearDown(() => calloc.free(native));
      native.ref
        ..direction = bindings.AUD_IO_DUPLEX
        ..sample_rate = 48000
        ..output_channels = 2
        ..input_channels = 1
        ..max_frames = 512
        ..buffer_frames = 192
        ..burst_frames = 96
        ..generation = 3
        ..performance_mode = bindings.AUD_IO_PERFORMANCE_LOW_LATENCY
        ..exclusive = 1
        ..time_source = AUD_TIME_SOURCE_HARDWARE;
      AudIoNativeString.write(native.ref.output_device_id, 128, '3');
      AudIoNativeString.write(native.ref.input_device_id, 128, '4');
      AudIoNativeString.write(native.ref.backend, 64, 'oboe/aaudio');
      final format = native.ref.toDart();
      expect(format.direction, AudIoDirection.duplex);
      expect(format.sampleRate, 48000);
      expect(format.outputChannels, 2);
      expect(format.inputChannels, 1);
      expect(format.maxFrames, 512);
      expect(format.bufferFrames, 192);
      expect(format.burstFrames, 96);
      expect(format.generation, 3);
      expect(format.performanceMode, AudIoPerformanceMode.lowLatency);
      expect(format.exclusive, isTrue);
      expect(format.timeSource, AudTimeSource.hardware);
      expect(format.outputDeviceId, '3');
      expect(format.inputDeviceId, '4');
      expect(format.backend, 'oboe/aaudio');
      expect(format.bufferDuration, const Duration(milliseconds: 4));
      expect(
        format.toString(),
        'AudIoStreamFormat(oboe/aaudio, 48000 Hz, 2 out, 1 in, buffer 192, '
        'generation 3)',
      );
      native.ref.sample_rate = 0;
      expect(native.ref.toDart().bufferDuration, Duration.zero);
    });
  });
}
