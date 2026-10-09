// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:aud_audio_io/src/aud_audio_io_bindings_generated.dart'
    as bindings;
import 'package:test/test.dart';

void main() {
  group('AudIoException', () {
    test('resultName(code) names the codes of the ABI and of the IO', () {
      expect(
        [
          for (final code in [
            bindings.AUD_IO_ERROR_PERMISSION,
            bindings.AUD_IO_ERROR_NO_DEVICE,
            bindings.AUD_IO_ERROR_DEVICE,
            bindings.AUD_IO_ERROR_INTERRUPTED,
            AUD_ERROR_STATE,
            -999,
          ])
            AudIoException.resultName(code),
        ],
        [
          'AUD_IO_ERROR_PERMISSION',
          'AUD_IO_ERROR_NO_DEVICE',
          'AUD_IO_ERROR_DEVICE',
          'AUD_IO_ERROR_INTERRUPTED',
          'AUD_ERROR_STATE',
          'AUD_RESULT_-999',
        ],
      );
    });

    test('check(result, what) passes results and throws errors', () {
      expect(AudIoException.check(3, 'count'), 3);
      expect(
        () => AudIoException.check(bindings.AUD_IO_ERROR_NO_DEVICE, 'open'),
        throwsA(
          isA<AudIoException>()
              .having((e) => e.code, 'code', bindings.AUD_IO_ERROR_NO_DEVICE)
              .having((e) => e.codeName, 'codeName', 'AUD_IO_ERROR_NO_DEVICE')
              .having(
                (e) => e.message,
                'message',
                'Could not open: AUD_IO_ERROR_NO_DEVICE',
              )
              .having(
                (e) => e.toString(),
                'toString',
                'AudIoException(AUD_IO_ERROR_NO_DEVICE): '
                    'Could not open: AUD_IO_ERROR_NO_DEVICE',
              ),
        ),
      );
    });
  });
}
