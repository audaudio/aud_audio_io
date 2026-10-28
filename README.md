# aud_audio_io

English | [Deutsch](README.de.md)

[![CI](https://github.com/audaudio/aud_audio_io/actions/workflows/quick_check.yaml/badge.svg)](https://github.com/audaudio/aud_audio_io/actions/workflows/quick_check.yaml)

Audio device IO of the Audanika Audio Engine: enumeration, hot-plug, duplex
multi-channel streams with timestamps; Oboe on Android, miniaudio elsewhere.

Part of the Audanika Audio Engine; planned in
[aud_audio_pm](https://github.com/audaudio/aud_audio_pm). The streams render
through the C ABI of `aud_audio_core`, with a stream time for every block
and recovery from route changes and interruptions on their own. Oboe on
Android, miniaudio with AVAudioSession on iOS, on the HAL on macOS and on
WASAPI on Windows; Linux runs the null device until its backend follows.

## Motivation

The engine needs one way to reach the audio devices of every platform, with
the guarantees its realtime contract asks for:

- One stream for output, input and full duplex on iOS and Android
  (ticket 21, step S3), on macOS (ticket 37) and on Windows (ticket 38),
  the macOS and Windows parts of S3b; Linux follows with its part of S3b
  and runs the null device until then
- Blocks of at most `maxFrames` frames, each with its `AudStreamTime`:
  sample position, host time with source and accuracy, latency per
  direction (decision time-001)
- Nothing allocates, locks, logs or calls into Dart on the audio thread
  (interop-001)
- Streams recover from disconnects, route and rate changes and
  interruptions on their own and report every case (lifecycle-001)
- Counters and a latency probe that measure a device
- The audio thread on the fast cores of Android devices (io-003, ticket
  30): a performance hint session from Android 12, below it an affinity to
  the cores above the slowest clock; `AudIoCounters.placement` (an
  `AudIoPlacement`: `system`, `hint`, `affinity` or `refused`) and
  `fastCpuShare` tell where it ran

## Installation

The package is not published to pub.dev; add it as a git dependency to the
`pubspec.yaml` of a Flutter app or package and run `flutter pub get`:

```yaml
dependencies:
  aud_audio_io:
    git:
      url: git@github.com:audaudio/aud_audio_io.git
      tag_pattern: "{{version}}"
    version: ^0.4.0
```

The package needs the Flutter SDK: on Android it reaches the devices, the
audio focus and the microphone permission through `package:jni`. An app
that records adds `NSMicrophoneUsageDescription` to its `Info.plist` on
iOS and `<uses-permission android:name="android.permission.RECORD_AUDIO" />`
to its `AndroidManifest.xml` on Android; `UIBackgroundModes` with `audio`
keeps an iOS app playing in the background.

## Usage

A sine on the default output:

```dart
import 'package:aud_audio_io/aud_audio_io_ffi.dart';

Future<void> main() async {
  final session = AudIoSessionFfi();
  final stream = session.open(
    const AudIoStreamConfig(),
    render: AudIoStreamFfi.sineRender,
  );
  stream.start();
  await Future<void>.delayed(const Duration(seconds: 1));
  print(stream.format); // backend, rate, channels, buffer, generation
  print(stream.counters.callbacks);
  session.dispose();
}
```

A graph of `aud_audio_graph` renders through the stream; after a change
of the device's format the client runs the graph's route-change sequence
and acknowledges the new format:

```dart
final stream = session.open(
  AudIoStreamConfig(maxFrames: graph.maxFrames),
  render: Native.addressOf(aud_graph_render), // aud_audio_graph_ffi.dart
  user: graph.pointer.cast(),
);
graph.prepare(
  sampleRate: stream.format.sampleRate,
  maxFrames: stream.format.maxFrames,
);
graph.start();
stream.notifications
    .where((n) => n.type == AudIoNotificationType.formatChanged)
    .listen((n) {
      graph.suspend();
      graph.prepare(sampleRate: n.sampleRate);
      graph.resume();
      stream.acknowledge(n.generation);
    });
stream.start();
```

The example app in [`example`](example) lists the devices, plays a sine,
monitors the input and measures the latency of a device with
`AudIoProbe`; its integration test runs on simulators, emulators and
devices.

## Limitations

- Linux has no device backend yet and runs the null device, which keeps
  time without hardware
- The package renders through a native render function only: the audio
  thread never calls into Dart, so a render function is written in C or
  C++ and reached through FFI
- It needs the Flutter SDK; it is no pure Dart package
- A new rate or channel count holds the render function until the client
  acknowledges the format, unless the render function follows the format
  on its own with `followFormat`
- Recording needs the microphone permission of the platform, see
  [Installation](#installation)

## Documentation

- [Audio IO](https://audaudio.github.io/io/) on the documentation site
- [The plan of ticket 21](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/tickets/2026-10-09-21-build-the-audio-io-for-ios-and-android.md)
  with its implementation notes, measurements and findings
- The C API: [`src/aud_audio_io.h`](src/aud_audio_io.h)
- The guides in [`doc/guides`](doc/guides)
- The blog posts in [`doc/blog`](doc/blog)

## How it works

- A session per app owns the backend and wakes one listener; its streams
  open devices and call a native render function on the audio thread.
- The callback splits the device's blocks into blocks of at most
  `maxFrames`, de-interleaves them into planar buses and fills the stream
  time; after a gap the sample position runs on by the frames the device
  was away, so the time filters of the engine reset.
- A worker thread per stream reopens a lost device with backoff and
  restarts after interruptions. A new rate or channel count holds the
  render function - the stream plays silence - until the client
  acknowledges the format; a render function that follows the format on
  its own, like those of the package, opts out with `followFormat`.
- The audio thread posts notifications into a lock-free queue; a
  notification thread wakes the Dart listener.
- On macOS and Windows the backend lists the devices of the platform -
  the HAL, the endpoints of WASAPI - and always opens them by id; a stream
  without an id follows the default device by reopening (io-004, io-005).
  On Windows the streams run in shared mode at the mix format's rate with
  the low-latency periods of `IAudioClient3`.
- The null device keeps time without hardware and takes injected faults:
  the native tests run on it under ASan/UBSan, the RealtimeSanitizer and
  ThreadSanitizer (`node scripts/test-native.js`).
- Oboe and miniaudio are vendored (`src/third_party/NOTICES.md`); the
  Android libraries are linked with 16 KB pages
  (`node scripts/check-page-size.js`). `node scripts/generate-bindings.js`
  and `node scripts/generate-android-bindings.js` regenerate the bindings.

## Status

Version 0.4.0, before 1.0 — the API may change between minor versions. The
device backends of iOS, Android, macOS and Windows are in place; Linux
follows.

## Contributing

Tickets run through the `gg` workflow: see the
[Develop Guide](doc/guides/develop-guide.md) and the
[Review Guide](doc/guides/for-ai/ai-review-guide.md).

## License

MIT — see [LICENSE](LICENSE).
