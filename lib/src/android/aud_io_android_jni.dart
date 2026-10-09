// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Runs on Android only, where a JVM exists; the example app exercises it on
// the emulator and the devices. The logic it serves is tested on the host
// in aud_io_android_platform_test.dart.
// coverage:ignore-file

import 'package:jni/jni.dart';

import 'aud_io_android_api.dart';
import 'aud_io_android_bindings.dart';

// #############################################################################
/// The [AudIoAndroidApi] through JNI: `AudioManager`, `AudioFocusRequest`
/// and the permission calls of `Context` and `Activity`.
class AudIoAndroidJni implements AudIoAndroidApi {
  /// Reaches Java through the application [context] and, for the
  /// permission dialog, the [activity] of the Flutter engine.
  AudIoAndroidJni({required JObject context, JObject? Function()? activity})
    : _context = context.as(Context.type),
      _activity = activity ?? (() => null);

  // ...........................................................................
  static const _recordAudio = 'android.permission.RECORD_AUDIO';
  static const _permissionRequestCode = 0x4144; // "AD"
  static const _streamMusic = 3;

  final Context _context;
  final JObject? Function() _activity;
  late final AudioManager _manager = _context
      .getSystemService$1(Context.AUDIO_SERVICE)!
      .as(AudioManager.type);
  AudioFocusRequest? _focusRequest;
  AudioManager$OnAudioFocusChangeListener? _listener;

  // ...........................................................................
  @override
  int get sdkLevel => Build$VERSION.SDK_INT;

  @override
  List<AudIoAndroidDeviceInfo> devices() => using((arena) {
    final array = _manager.getDevices(AudioManager.GET_DEVICES_ALL)!
      ..releasedBy(arena);
    return [
      for (var i = 0; i < array.length; i++)
        _infoOf(array[i]!..releasedBy(arena), arena),
    ];
  });

  @override
  List<int> mediaDeviceIds() {
    if (sdkLevel < 33) return const [];
    return using((arena) {
      final attributes = _musicAttributes(arena);
      final list = _manager.getAudioDevicesForAttributes(attributes)!
        ..releasedBy(arena);
      return [for (final info in list.asDart()) (info!..releasedBy(arena)).id];
    });
  }

  @override
  bool get hasRecordPermission => using((arena) {
    final permission = _recordAudio.toJString()..releasedBy(arena);
    return _context.checkSelfPermission(permission) == 0;
  });

  @override
  void requestRecordPermission() {
    final activity = _activity();
    if (activity == null) return;
    using((arena) {
      final permissions = JArray.of<JString>(JString.type, [
        _recordAudio.toJString()..releasedBy(arena),
      ])..releasedBy(arena);
      final activityClass = JClass.forName('android/app/Activity')
        ..releasedBy(arena);
      activityClass.instanceMethodId(
        'requestPermissions',
        '([Ljava/lang/String;I)V',
      )(activity, jvoid.type, [permissions, JValueInt(_permissionRequestCode)]);
    });
  }

  @override
  int requestFocus(void Function(int change) onChange) {
    abandonFocus();
    final listener = AudioManager$OnAudioFocusChangeListener.implement(
      $AudioManager$OnAudioFocusChangeListener(
        onAudioFocusChange: onChange,
        // Java does not wait for Dart: the platform thread may be the
        // thread Dart runs on.
        onAudioFocusChange$async: true,
      ),
    );
    _listener = listener;
    if (sdkLevel < 26) {
      return _manager.requestAudioFocus$1(
        listener,
        _streamMusic,
        AudioManager.AUDIOFOCUS_GAIN,
      );
    }
    final request = using((arena) {
      final builder = AudioFocusRequest$Builder.new$1(
        AudioManager.AUDIOFOCUS_GAIN,
      )..releasedBy(arena);
      builder.setAudioAttributes(_musicAttributes(arena))?.releasedBy(arena);
      builder.setAcceptsDelayedFocusGain(true)?.releasedBy(arena);
      builder.setWillPauseWhenDucked(false)?.releasedBy(arena);
      builder.setOnAudioFocusChangeListener(listener)?.releasedBy(arena);
      return builder.build()!;
    });
    _focusRequest = request;
    return _manager.requestAudioFocus(request);
  }

  @override
  void abandonFocus() {
    final request = _focusRequest;
    final listener = _listener;
    if (request != null) {
      _manager.abandonAudioFocusRequest(request);
      request.release();
    } else if (listener != null) {
      using((arena) {
        final managerClass = JClass.forName('android/media/AudioManager')
          ..releasedBy(arena);
        managerClass.instanceMethodId(
          'abandonAudioFocus',
          '(Landroid/media/AudioManager\$OnAudioFocusChangeListener;)I',
        )(_manager, jint.type, [listener]);
      });
    }
    listener?.release();
    _focusRequest = null;
    _listener = null;
  }

  // ...........................................................................
  AudioAttributes _musicAttributes(Arena arena) {
    final builder = AudioAttributes$Builder()..releasedBy(arena);
    builder.setUsage(AudioAttributes.USAGE_MEDIA)?.releasedBy(arena);
    builder
        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
        ?.releasedBy(arena);
    return builder.build()!..releasedBy(arena);
  }

  static AudIoAndroidDeviceInfo _infoOf(AudioDeviceInfo info, Arena arena) {
    final channels = info.channelCounts?..releasedBy(arena);
    final rates = info.sampleRates?..releasedBy(arena);
    final name = info.productName?..releasedBy(arena);
    return AudIoAndroidDeviceInfo(
      id: info.id,
      name: name?.toString() ?? '',
      type: info.type$1,
      isSink: info.isSink,
      isSource: info.isSource,
      channelCounts: channels == null
          ? const []
          : List.unmodifiable(channels.getRange(0, channels.length)),
      sampleRates: rates == null
          ? const []
          : List.unmodifiable(rates.getRange(0, rates.length)),
    );
  }
}
