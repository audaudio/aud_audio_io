// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Proves that the Android libraries of aud_audio_io run on devices with
// 16 KB pages (io-002, ticket 21): every LOAD segment of every
// libaud_audio_io.so must be aligned to at least 16 KB. Build the example
// for Android first, e.g. `cd example && flutter build apk`, then run
//
//   node scripts/check-page-size.js            the example's build folder
//   node scripts/check-page-size.js <path>...  libraries or folders
//
// llvm-readelf comes from the NDK of the Android SDK, from LLVM_READELF or
// from the PATH.

'use strict';

const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const library = 'libaud_audio_io.so';
const minimumAlign = 0x4000;

function readelf() {
  const candidates = [process.env.LLVM_READELF];
  const sdk =
    process.env.ANDROID_HOME ||
    process.env.ANDROID_SDK_ROOT ||
    path.join(os.homedir(), 'Library', 'Android', 'sdk');
  const ndkRoot = process.env.ANDROID_NDK_HOME || path.join(sdk, 'ndk');
  if (fs.existsSync(ndkRoot)) {
    const ndks = fs.existsSync(path.join(ndkRoot, 'toolchains'))
      ? [ndkRoot]
      : fs
          .readdirSync(ndkRoot)
          .sort()
          .reverse()
          .map((version) => path.join(ndkRoot, version));
    for (const ndk of ndks) {
      const prebuilt = path.join(ndk, 'toolchains', 'llvm', 'prebuilt');
      if (!fs.existsSync(prebuilt)) continue;
      for (const host of fs.readdirSync(prebuilt)) {
        candidates.push(path.join(prebuilt, host, 'bin', 'llvm-readelf'));
      }
    }
  }
  candidates.push('llvm-readelf', '/opt/homebrew/opt/llvm/bin/llvm-readelf');
  for (const candidate of candidates.filter(Boolean)) {
    const probe = spawnSync(candidate, ['--version'], { encoding: 'utf8' });
    if (probe.status === 0) return candidate;
  }
  throw new Error('llvm-readelf not found; set LLVM_READELF');
}

function findLibraries(target, found) {
  if (!fs.existsSync(target)) return;
  const stat = fs.statSync(target);
  if (stat.isFile()) {
    if (path.basename(target) === library) found.push(target);
    return;
  }
  for (const entry of fs.readdirSync(target)) {
    findLibraries(path.join(target, entry), found);
  }
}

// The alignments of the LOAD segments of `file`.
function loadAlignments(tool, file) {
  const result = spawnSync(tool, ['-lW', file], { encoding: 'utf8' });
  if (result.status !== 0) {
    throw new Error(`llvm-readelf failed on ${file}: ${result.stderr}`);
  }
  return result.stdout
    .split('\n')
    .filter((line) => line.trim().startsWith('LOAD'))
    .map((line) => {
      const fields = line.trim().split(/\s+/);
      return Number.parseInt(fields[fields.length - 1], 16);
    });
}

function main() {
  const targets = process.argv.slice(2);
  if (targets.length === 0) targets.push(path.join(root, 'example', 'build'));
  const libraries = [];
  for (const target of targets) findLibraries(path.resolve(target), libraries);
  if (libraries.length === 0) {
    console.error(`No ${library} found in ${targets.join(', ')}; build the ` +
      'example for Android first: cd example && flutter build apk');
    process.exit(1);
  }
  const tool = readelf();
  let failed = false;
  for (const file of libraries) {
    const alignments = loadAlignments(tool, file);
    const ok = alignments.length > 0 && alignments.every((a) => a >= minimumAlign);
    const shown = alignments.map((a) => `0x${a.toString(16)}`).join(' ');
    console.log(`${ok ? 'ok    ' : 'FAILED'} ${path.relative(root, file)}: LOAD ${shown}`);
    failed = failed || !ok;
  }
  process.exit(failed ? 1 : 0);
}

main();
