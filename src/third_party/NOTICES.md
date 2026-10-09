# Third-party notices of aud_audio_io

| Component | Path | Version | License |
| --- | --- | --- | --- |
| miniaudio | `miniaudio/miniaudio.h` | 0.11.25 (2026-03-04) | public domain or MIT-0, `miniaudio/LICENSE` |
| Oboe | `oboe/include`, `oboe/src` | 1.11.0 (2026-09-15) | Apache-2.0, `oboe/LICENSE` |

Sources: https://github.com/mackron/miniaudio and https://github.com/google/oboe,
vendored on 2026-10-08 for ticket 5 and kept for ticket 21 (decision 4 of
its plan review: vendored, not fetched at build time). The Android build
hook compiles all of Oboe's sources - AAudio, the OpenSL ES fallback below
API 27 and the conversions of its flow graph are all reachable - and links
them with hidden symbols; miniaudio's Core Audio device is compiled into the
library on iOS only, until the desktop backends of S3b to S3d use it.
