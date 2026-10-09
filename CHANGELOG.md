# Changelog

## Unreleased

### Changed

- Build the audio IO for iOS and Android (S3)
- Replace the spike stream by sessions, devices and streams on the render interface of ABI 0.3 (aud_audio_io.h, API 2)
- Split and de-interleave device callbacks into planar blocks of at most maxFrames with an AudStreamTime: position, host time, source, accuracy, latency per direction
- Recover from disconnects, interruptions, route and rate changes on a worker; hold the renderer until the client acknowledges a new format, unless it follows the format
- Report notifications through a notification thread; count callbacks, periods, xruns, recoveries and timestamp jitter
- Add the Android backend on Oboe (output, input, full duplex, latency tuner, AAudio timestamps, 16 KB pages, hidden symbols)
- Add the iOS backend on miniaudio with an AVAudioSession of its own: routes, interruptions, media services reset, permission
- Reach AudioManager devices, the audio focus and RECORD_AUDIO through package:jni on Android
- Add the Dart API: AudIoSession, AudIoStream, AudIoProbe and their value types
- Add the null backend with injectable faults and native tests under ASan/UBSan, RTSan with a probe and TSan
- Add the sine, monitor and latency probe render functions and the example app with its integration test
- Add scripts to test natively, check the page size and generate the bindings
- Count the input that waits in miniaudio's duplex ring buffer in the iOS input time
- Make the probe's click a 5 ms burst of 2 kHz and report the largest input level
- Add a measurement integration test that reports a device's callbacks, latency and round trip
- Format the changelog entry of ticket 21

## 0.1.0 - 2026-10-08

### Added

- Add the spike stream on miniaudio and Oboe

## 0.0.2 - 2026-10-08

### Added

- Initial boilerplate

### Changed

- Record the gg commit state
- Set up GitHub repo settings and branch rules
- Update dev dependencies
