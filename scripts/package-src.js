// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Resolves the `src` directory of a package through the package config of
// the repo, so that git, path and pub dependencies all work. Shared by
// scripts/test-native.js and scripts/generate-bindings.js.

'use strict';

const fs = require('node:fs');
const path = require('node:path');

// The `src` directory of the package `name`, seen from the repo `root`.
function packageSrc(root, name) {
  const configPath = path.join(root, '.dart_tool', 'package_config.json');
  if (!fs.existsSync(configPath)) {
    throw new Error(`Run dart pub get first: ${configPath} is missing`);
  }
  const config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  const entry = config.packages.find((p) => p.name === name);
  if (!entry) throw new Error(`Package ${name} is not in the package config`);
  const rootUri = new URL(entry.rootUri, `file://${configPath}`);
  return path.join(decodeURIComponent(rootUri.pathname), 'src');
}

module.exports = { packageSrc };
