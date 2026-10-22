# Changelog

## Unreleased

### Changed

- Build and test aud_audio_io on Windows

## 0.3.5 - 2026-10-22

### Added

- Add the macOS device on the HAL (ticket 37)

- src/aud_io_macos.mm: miniaudio's Core Audio device on the devices of the HAL

- The devices with route, channels and rates; defaults followed by reopening

- Device loss, rate and channel changes, data source and overloads observed

- Latency estimates from device latency, safety offset and stream latency

- The hook builds it for macOS; Windows and Linux keep the null device

- Add macOS to the German README (ticket 37)

- The German goals name the macOS device as the English ones do

- Add the macOS device to the index

- Add the microphone permission before macOS 14

- Before macOS 14 the permission comes from AVCaptureDevice instead of always being granted

- A device whose open failed no longer removes the listeners of another stream on the same device

- Add tests for the macOS backend's loss and rate decisions

- One unplug reaches the stream once: a device reports only its first loss, a later report of the device list, the default or IsAlive closed the device the stream had just reopened

- A rate change reopens only a stream that asked for no rate, weighed by each device's own rate at open instead of the stream's, so a duplex input no longer counts against its output

- The decisions on the HAL's devices live in hal:: of aud_io_internal.hpp: rates, routes, lost devices, the default device, the loss of a changed property and the single loss report

- test/native/aud_io_macos_test.cpp covers them in the native tests

- Add the device serial that ties a loss to its device

- Every device gets a serial when it opens and names itself in streamDeviceLost; the worker drops a recovery for a device the stream no longer runs on, so a second or late report of one loss no longer closes the device the stream had reopened

- This covers every backend: two route changes or a route change and the media services reset on iOS, the device list, the default and IsAlive on macOS, both Oboe streams of a duplex device on Android, and a loss that arrives after the end of an interruption or a failed start replaced the device

- Pending recoveries merge: the first report of one device stands, a newer device's replaces it, a start that names no device always runs

- The per-device LossReport of the macOS backend is gone: the stream decides for all backends

- iOS reports the reset of the media services through its devices; sessionAllDevicesLost is gone

- What the iOS backend decides on a route change lives in ios:: of aud_io_internal.hpp: why it changed and when a device reopens; test/native/aud_io_ios_test.cpp covers it

- Native tests: two losses of one device disconnect once, a late loss of a replaced device keeps the new one, the new device's own loss still recovers


## 0.3.4 - 2026-10-15

### Added

- Add the audio thread on the fast cores of Android devices

- From Android 12 a performance hint session aimed at half of each burst learns the work of each callback

- On Android 10 and 11, and without a session, the callback thread binds itself to the cores above the slowest clock and binds again when the system resets it

- The counters report the placement, the fast cores, the resets, the refusals and the share of sampled callbacks on a fast core (API 3)

- Add a check of the affinity that allows isolated cores

- The kernel leaves the cores that core control isolates out of the affinity it returns; the check now accepts a mask within the fast cores instead of setting the affinity anew every second

- Add the placement of the system after a stop

- A stopped stream reports no placement of a thread that no longer runs

- Add the fixes of the review of ticket 30

- fastCoresOf() and affinityHolds() hold the choice of the fast cores and the check of the affinity, with native tests

- The manager of the performance hint is looked up on the control thread; only the session opens in the first callback, the exception io-003 accepts

- The counts use bump() of the internal header; named constants for the API level and the cores of the masks, which stay within a cpu_set_t on 32-bit Android

- The placements are documented as the code sets them; the README names AudIoPlacement


## 0.3.3 - 2026-10-14

### Added

- Add the example lock of the current version

- The lock of the example records the version of the package it plays


### Changed

- S9b: Limit the voices of the sampler by the CPU power of the device

## 0.3.2 - 2026-10-12

### Added

- Add review fixes of ticket 26

- Remove the blank lines between the entries under Unreleased in CHANGELOG.md


## 0.3.1 - 2026-10-12

## 0.3.0 - 2026-10-09

### Changed

- Split the Dart API into neutral and ffi parts
- Describe the neutral and ffi APIs in the README

## 0.2.0 - 2026-10-09

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
