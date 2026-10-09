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
  group('AudIoDevice', () {
    test('fromNative(native) reads every field', () {
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
      final device = AudIoDevice.fromNative(native.ref);
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

    test('== and hashCode compare every field', () {
      const a = AudIoDevice(
        id: '1',
        name: 'A',
        directions: AudIoDirection.output,
        sampleRates: [48000],
      );
      const differing = [
        AudIoDevice(id: '2', name: 'A', directions: AudIoDirection.output),
        AudIoDevice(
          id: '1',
          name: 'A',
          directions: AudIoDirection.output,
          sampleRates: [44100],
        ),
        AudIoDevice(
          id: '1',
          name: 'A',
          directions: AudIoDirection.output,
          sampleRates: [48000, 96000],
        ),
      ];
      const same = AudIoDevice(
        id: '1',
        name: 'A',
        directions: AudIoDirection.output,
        sampleRates: [48000],
      );
      expect(a, same);
      expect(a.hashCode, same.hashCode);
      expect(
        [for (final other in differing) a == other],
        [false, false, false],
      );
      expect(a == Object(), isFalse);
    });
  });
}
