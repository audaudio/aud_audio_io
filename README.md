# aud_audio_io

Audio device IO of the Audanika Audio Engine: enumeration, hot-plug, duplex multi-channel streams with timestamps; Oboe on Android, miniaudio elsewhere.

Part of the Audanika Audio Engine; planned in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

## The spike stream (ticket 5)

`AudIoStream` opens one output stream that pulls interleaved float blocks
from a native render callback on the audio thread and measures the
callback timing. miniaudio 0.11.25 serves macOS and iOS (Windows and
Linux follow with S0-desktop), Oboe 1.11.0 serves Android; both are
vendored under `src/third_party` and compiled by the build hook, see
`src/third_party/NOTICES.md`.

```dart
import 'package:aud_audio_io/aud_audio_io.dart';

final stream = AudIoStream.open(
  render: AudEngine.renderCallback,   // or AudIoStream.sineRender
  user: engine.handle,
  sampleRate: 48000,
  channels: 2,
  framesPerCallback: 0,               // the backend's default
  useNullBackend: false,              // true in tests
);
stream.start();
stream.backendName;                    // 'miniaudio/Core Audio', 'oboe/aaudio'
stream.framesPerCallback;              // the negotiated block size
stream.stats;                          // callbacks, period, late, xruns, callback time
stream.stop();
stream.close();
```

`AudIoException` carries the ABI result code of a refused call. The C API
is in `src/aud_audio_io.h`; regenerate the bindings with
`dart run ffigen --config ffigen.yaml`.
