# aud_audio_io

Audio-Geräte-IO der Audanika Audio Engine: Geräte, Hot-Plug sowie
Ausgabe-, Eingabe- und Duplex-Streams, die über die C-ABI von
`aud_audio_core` rendern, mit einer Stream-Zeit für jeden Block und einer
selbstständigen Erholung von Routenwechseln und Unterbrechungen. Oboe auf
Android, miniaudio mit AVAudioSession auf iOS.

Teil der Audanika Audio Engine; geplant in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

## Ziele

- Ein Stream für Ausgabe, Eingabe und Vollduplex auf iOS und Android
  (Ticket 21, Schritt S3); macOS, Windows und Linux folgen mit S3b bis S3d
  und laufen bis dahin auf dem Null-Gerät
- Blöcke von höchstens `maxFrames` Frames, jeder mit seiner
  `AudStreamTime`: Sample-Position, Host-Zeit mit Quelle und Genauigkeit,
  Latenz je Richtung (Entscheidung time-001)
- Auf dem Audio-Thread wird nichts alloziert, gesperrt, geloggt oder nach
  Dart gerufen (interop-001)
- Streams erholen sich selbst von Verbindungsabbrüchen, Routen- und
  Ratenwechseln und Unterbrechungen und melden jeden Fall (lifecycle-001)
- Zähler und eine Latenzsonde, die ein Gerät vermessen

## Installation

```yaml
dependencies:
  aud_audio_io:
    git:
      url: git@github.com:audaudio/aud_audio_io.git
      tag_pattern: "{{version}}"
    version: ^0.2.0
```

Das Paket braucht das Flutter-SDK: Auf Android erreicht es die Geräte, den
Audiofokus und die Mikrofonberechtigung über `package:jni`. Eine App, die
aufnimmt, trägt auf iOS `NSMicrophoneUsageDescription` in ihre
`Info.plist` ein und auf Android
`<uses-permission android:name="android.permission.RECORD_AUDIO" />` in
ihr `AndroidManifest.xml`; `UIBackgroundModes` mit `audio` lässt eine
iOS-App im Hintergrund weiterspielen.

## Dokumentation

- [Audio IO](https://audaudio.github.io/io/) auf der Dokumentationsseite
- [Der Plan von Ticket 21](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/tickets/2026-10-09-21-build-the-audio-io-for-ios-and-android.md)
  mit Umsetzungsnotizen, Messungen und Befunden
- Die C-API: [`src/aud_audio_io.h`](src/aud_audio_io.h)
- Die Guides in [`doc/guides`](doc/guides)

## Code-Beispiele

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
- Das Null-Gerät hält die Zeit ohne Hardware und nimmt eingespeiste Fehler
  an: Die nativen Tests laufen darauf unter ASan/UBSan, dem
  RealtimeSanitizer und dem ThreadSanitizer (`node scripts/test-native.js`).
- Oboe und miniaudio sind eingebunden (`src/third_party/NOTICES.md`); die
  Android-Bibliotheken werden mit 16-KB-Seiten gelinkt
  (`node scripts/check-page-size.js`). `node scripts/generate-bindings.js`
  und `node scripts/generate-android-bindings.js` erzeugen die Bindings neu.

## Mitwirken

Tickets laufen durch den `gg`-Workflow: siehe den
[Develop Guide](doc/guides/develop-guide.md) und den
[Review Guide](doc/guides/for-ai/ai-review-guide.md).
