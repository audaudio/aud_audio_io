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

// Builds the stream with the backend of the target platform: Oboe compiled
// from the vendored sources on Android, miniaudio everywhere else. The
// miniaudio translation unit is Objective-C++ (`.mm`) because miniaudio
// talks to AVAudioSession on iOS; the sources therefore mix languages, no
// language flag is passed - clang picks it by extension - and the C++
// runtime is linked explicitly (build-001). The ABI header comes from
// aud_audio_core, resolved through the package config.
void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;
    final packageName = input.packageName;
    final targetOS = input.config.code.targetOS;
    final android = targetOS == OS.android;
    final apple = targetOS == OS.iOS || targetOS == OS.macOS;
    final cbuilder = CBuilder.library(
      name: packageName,
      assetName: 'src/${packageName}_bindings_generated.dart',
      sources: [
        'src/aud_io_sine.cpp',
        if (android) 'src/aud_io_oboe.cpp' else 'src/aud_io_miniaudio.mm',
        if (android) ...await oboeSources(input.packageRoot),
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
      libraries: [
        if (android) ...['c++_static', 'c++abi', 'log', 'OpenSLES', 'm'],
        if (apple) 'c++',
        if (!android && !apple) 'stdc++',
      ],
      frameworks: [
        'Foundation',
        if (apple) ...['CoreFoundation', 'CoreAudio', 'AudioToolbox'],
        if (targetOS == OS.iOS) 'AVFoundation',
        if (targetOS == OS.macOS) 'AudioUnit',
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
