// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Regenerates lib/src/aud_audio_io_bindings_generated.dart from
// src/aud_audio_io.h with ffigen: `node scripts/generate-bindings.js`. The
// header includes aud_abi.h of aud_audio_core, which this script resolves
// through the package config, so git, path and pub dependencies all work.

'use strict';

const { spawnSync } = require('node:child_process');
const path = require('node:path');

const { packageSrc: resolveSrc } = require('./package-src');

const root = path.resolve(__dirname, '..');

function main() {
  const result = spawnSync(
    'dart',
    [
      'run',
      'ffigen',
      '--config',
      'ffigen.yaml',
      '--compiler-opts',
      `-I${resolveSrc(root, 'aud_audio_core')}`,
    ],
    { cwd: root, stdio: 'inherit' },
  );
  process.exit(result.status ?? 1);
}

main();
