// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_io_platform.dart';

// #############################################################################
/// The platform of a session outside Flutter: the native backend alone.
/// Inside Flutter, android/aud_io_flutter_platform.dart takes its place.
AudIoPlatform currentPlatform() => const AudIoPlatform.native();
