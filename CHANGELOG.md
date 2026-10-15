# Changelog

## Unreleased

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
