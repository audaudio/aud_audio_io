# aud_audio_io

[English](README.md) | Deutsch

[![CI](https://github.com/audaudio/aud_audio_io/actions/workflows/quick_check.yaml/badge.svg)](https://github.com/audaudio/aud_audio_io/actions/workflows/quick_check.yaml)

Audio-Geräte-IO der Audanika Audio Engine: Geräteliste, Hot-Plug,
mehrkanalige Duplex-Streams mit Zeitstempeln; Oboe auf Android, miniaudio
sonst.

Teil der Audanika Audio Engine; geplant in
[aud_audio_pm](https://github.com/audaudio/aud_audio_pm). Die Streams
rendern über die C-ABI von `aud_audio_core`, mit einer Stream-Zeit für
jeden Block und einer selbstständigen Erholung von Routenwechseln und
Unterbrechungen. Oboe auf Android, miniaudio mit AVAudioSession auf iOS,
auf dem HAL unter macOS und auf WASAPI unter Windows; Linux läuft auf dem
Null-Gerät, bis sein Backend folgt.

## Motivation

Die Engine braucht einen Weg zu den Audiogeräten jeder Plattform, mit den
Garantien, die ihr Realtime-Vertrag verlangt:

- Ein Stream für Ausgabe, Eingabe und Vollduplex auf iOS und Android
  (Ticket 21, Schritt S3), auf macOS (Ticket 37) und auf Windows (Ticket
  38), die macOS- und Windows-Teile von S3b; Linux folgt mit seinem Teil
  von S3b und läuft bis dahin auf dem Null-Gerät
- Blöcke von höchstens `maxFrames` Frames, jeder mit seiner
  `AudStreamTime`: Sample-Position, Host-Zeit mit Quelle und Genauigkeit,
  Latenz je Richtung (Entscheidung time-001)
- Auf dem Audio-Thread wird nichts alloziert, gesperrt, geloggt oder nach
  Dart gerufen (interop-001)
- Streams erholen sich selbst von Verbindungsabbrüchen, Routen- und
  Ratenwechseln und Unterbrechungen und melden jeden Fall (lifecycle-001)
- Zähler und eine Latenzsonde, die ein Gerät vermessen
- Der Audio-Thread auf den schnellen Kernen von Android-Geräten (io-003,
  Ticket 30): ab Android 12 eine Performance-Hint-Session, darunter eine
  Affinität zu den Kernen über dem langsamsten Takt;
  `AudIoCounters.placement` (ein `AudIoPlacement`: `system`, `hint`,
  `affinity` oder `refused`) und `fastCpuShare` zeigen, wo er lief

## Installation

Das Paket ist nicht auf pub.dev veröffentlicht; trage es als
Git-Abhängigkeit in die `pubspec.yaml` einer Flutter-App oder eines
Flutter-Pakets ein und führe `flutter pub get` aus:

```yaml
dependencies:
  aud_audio_io:
    git:
      url: git@github.com:audaudio/aud_audio_io.git
      tag_pattern: "{{version}}"
    version: ^0.4.0
```

Das Paket braucht das Flutter-SDK: Auf Android erreicht es die Geräte, den
Audiofokus und die Mikrofonberechtigung über `package:jni`. Eine App, die
aufnimmt, trägt auf iOS `NSMicrophoneUsageDescription` in ihre
`Info.plist` ein und auf Android
`<uses-permission android:name="android.permission.RECORD_AUDIO" />` in
ihr `AndroidManifest.xml`; `UIBackgroundModes` mit `audio` lässt eine
iOS-App im Hintergrund weiterspielen.

## Verwendung

Ein Sinus auf der Standardausgabe:

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

Ein Graph von `aud_audio_graph` rendert über den Stream; nach einem
Formatwechsel des Geräts führt der Client die Routenwechsel-Sequenz des
Graphen aus und bestätigt das neue Format:

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

Die Beispiel-App in [`example`](example) listet die Geräte, spielt einen
Sinus, hört den Eingang mit und misst die Latenz eines Geräts mit
`AudIoProbe`; ihr Integrationstest läuft auf Simulatoren, Emulatoren und
Geräten.

## Grenzen

- Linux hat noch kein Geräte-Backend und läuft auf dem Null-Gerät, das die
  Zeit ohne Hardware hält
- Das Paket rendert nur über eine native Render-Funktion: Der Audio-Thread
  ruft nie nach Dart, eine Render-Funktion ist also in C oder C++
  geschrieben und wird über FFI erreicht
- Es braucht das Flutter-SDK; es ist kein reines Dart-Paket
- Eine neue Rate oder Kanalzahl hält die Render-Funktion an, bis der
  Client das Format bestätigt, es sei denn, die Render-Funktion folgt dem
  Format mit `followFormat` selbst
- Aufnehmen braucht die Mikrofonberechtigung der Plattform, siehe
  [Installation](#installation)

## Dokumentation

- [Audio IO](https://audaudio.github.io/io/) auf der Dokumentationsseite
- [Der Plan von Ticket 21](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/tickets/2026-10-09-21-build-the-audio-io-for-ios-and-android.md)
  mit Umsetzungsnotizen, Messungen und Befunden
- Die C-API: [`src/aud_audio_io.h`](src/aud_audio_io.h)
- Die Guides in [`doc/guides`](doc/guides)
- Die Blog-Posts in [`doc/de/blog`](doc/de/blog)

## Funktionsweise

- Eine Session je App besitzt das Backend und weckt einen Listener; ihre
  Streams öffnen Geräte und rufen auf dem Audio-Thread eine native
  Render-Funktion.
- Der Callback teilt die Blöcke des Geräts in Blöcke von höchstens
  `maxFrames`, entschachtelt sie in planare Busse und füllt die
  Stream-Zeit; nach einer Lücke läuft die Sample-Position um die Frames
  weiter, die das Gerät fehlte, sodass die Zeitfilter der Engine
  zurückgesetzt werden.
- Ein Worker-Thread je Stream öffnet ein verlorenes Gerät mit Backoff neu
  und startet nach Unterbrechungen wieder. Eine neue Rate oder
  Kanalzahl hält die Render-Funktion an - der Stream spielt Stille -,
  bis der Client das Format bestätigt; eine Render-Funktion, die dem
  Format selbst folgt, wie die des Pakets, verzichtet mit
  `followFormat` darauf.
- Der Audio-Thread legt Benachrichtigungen in eine lock-freie Queue; ein
  Benachrichtigungs-Thread weckt den Dart-Listener.
- Auf macOS und Windows listet das Backend die Geräte der Plattform - den
  HAL, die Endpunkte von WASAPI - und öffnet sie immer über ihre ID; ein
  Stream ohne ID folgt dem Standardgerät, indem er neu öffnet (io-004,
  io-005). Unter Windows laufen die Streams im Shared Mode mit der Rate
  des Mix-Formats und den Low-Latency-Perioden von `IAudioClient3`.
- Das Null-Gerät hält die Zeit ohne Hardware und nimmt eingespeiste Fehler
  an: Die nativen Tests laufen darauf unter ASan/UBSan, dem
  RealtimeSanitizer und dem ThreadSanitizer (`node scripts/test-native.js`).
- Oboe und miniaudio sind eingebunden (`src/third_party/NOTICES.md`); die
  Android-Bibliotheken werden mit 16-KB-Seiten gelinkt
  (`node scripts/check-page-size.js`). `node scripts/generate-bindings.js`
  und `node scripts/generate-android-bindings.js` erzeugen die Bindings neu.

## Status

Version 0.4.0, vor 1.0 — die API kann sich zwischen Minor-Versionen
ändern. Die Geräte-Backends für iOS, Android, macOS und Windows stehen;
Linux folgt.

## Mitwirken

Tickets laufen durch den `gg`-Workflow: siehe den
[Develop Guide](doc/guides/develop-guide.md) und den
[Review Guide](doc/guides/for-ai/ai-review-guide.md).

## Lizenz

MIT — siehe [LICENSE](LICENSE).
