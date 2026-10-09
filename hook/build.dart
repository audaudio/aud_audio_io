// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:isolate';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:logging/logging.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

// Builds the stream, the null backend and the backend of the target
// platform (ticket 21): Oboe compiled from the vendored sources on Android,
// miniaudio with AVAudioSession on iOS, the null backend alone elsewhere
// until the desktop backends arrive (S3b to S3d). The iOS backend is
// Objective-C++ (`.mm`); the sources therefore mix languages, no language
// flag is passed - clang picks it by extension - and the C++ runtime is
// linked explicitly (build-001). The symbols of the vendored libraries stay
// hidden; only the AUD_EXPORT functions are visible. Android links with
// 16 KB page alignment, which native_toolchain_c sets by default and
// scripts/check-page-size.js proves. The ABI header comes from
// aud_audio_core, resolved through the package config.
void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;
    final packageName = input.packageName;
    final targetOS = input.config.code.targetOS;
    final android = targetOS == OS.android;
    final iOS = targetOS == OS.iOS;
    final apple = iOS || targetOS == OS.macOS;
    final cbuilder = CBuilder.library(
      name: packageName,
      assetName: 'src/${packageName}_bindings_generated.dart',
      sources: [
        'src/aud_io_null.cpp',
        'src/aud_io_render.cpp',
        'src/aud_io_stream.cpp',
        if (iOS) 'src/aud_io_ios.mm',
        if (android) ...[
          'src/aud_io_android.cpp',
          ...await oboeSources(input.packageRoot),
        ],
      ],
      includes: [
        'src',
        await packageSrcDirectory('aud_audio_core'),
        if (android) ...[
          'src/third_party/oboe/include',
          'src/third_party/oboe/src',
        ],
      ],
      language: Language.c,
      std: 'c++17',
      flags: [
        '-fvisibility=hidden',
        '-fvisibility-inlines-hidden',
        if (iOS) '-fobjc-arc',
        if (android) ...[
          '-Wl,-z,max-page-size=16384',
          // The static C++ runtime stays inside the library.
          '-Wl,--exclude-libs,ALL',
          // Code nothing calls leaves the library.
          '-ffunction-sections',
          '-fdata-sections',
          '-Wl,--gc-sections',
        ],
      ],
      libraries: [
        if (android) ...['c++_static', 'c++abi', 'log', 'OpenSLES', 'm'],
        if (apple) 'c++',
        if (!android && !apple) 'stdc++',
      ],
      frameworks: [
        if (iOS) ...[
          'AVFoundation',
          'AudioToolbox',
          'CoreAudio',
          'CoreFoundation',
          'Foundation',
          'UIKit',
        ],
      ],
    );
    await cbuilder.run(
      input: input,
      output: output,
      logger: Logger('')
        ..level = Level.ALL
        ..onRecord.listen((record) => stdout.writeln(record.message)),
    );
  });
}

/// The `src` directory of [package], resolved through the package config.
Future<String> packageSrcDirectory(String package) async {
  final lib = await Isolate.resolvePackageUri(Uri.parse('package:$package/'));
  if (lib == null) throw StateError('Package $package is not resolvable');
  return lib.resolve('../src/').toFilePath();
}

/// The C++ sources of the vendored Oboe, relative to the package root.
Future<List<String>> oboeSources(Uri packageRoot) async {
  const directory = 'src/third_party/oboe/src';
  final root = Directory.fromUri(packageRoot.resolve(directory));
  final sources = <String>[];
  await for (final entity in root.list(recursive: true)) {
    if (entity is File && entity.path.endsWith('.cpp')) {
      sources.add(
        Uri.file(
          entity.path,
        ).toString().replaceFirst(packageRoot.toString(), ''),
      );
    }
  }
  sources.sort();
  return sources;
}
