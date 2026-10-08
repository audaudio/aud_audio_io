// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:async';

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:flutter/material.dart';

void main() {
  runApp(const AudIoExampleApp());
}

/// Plays the sine of the package on the device and shows the callback
/// timing.
class AudIoExampleApp extends StatefulWidget {
  /// Creates the example app.
  const AudIoExampleApp({super.key});

  @override
  State<AudIoExampleApp> createState() => _AudIoExampleAppState();
}

class _AudIoExampleAppState extends State<AudIoExampleApp> {
  AudIoStream? _stream;
  Timer? _timer;
  AudIoStats? _stats;

  void _toggle() {
    final stream = _stream;
    if (stream == null) {
      final opened = AudIoStream.open(render: AudIoStream.sineRender);
      opened.start();
      _stream = opened;
      _timer = Timer.periodic(const Duration(milliseconds: 500), (_) {
        setState(() => _stats = opened.stats);
      });
    } else {
      _timer?.cancel();
      stream.close();
      _stream = null;
    }
    setState(() {});
  }

  @override
  void dispose() {
    _timer?.cancel();
    _stream?.close();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final stream = _stream;
    final stats = _stats;
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('aud_audio_io')),
        body: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              FilledButton(
                onPressed: _toggle,
                child: Text(stream == null ? 'Play sine' : 'Stop'),
              ),
              if (stream != null) ...[
                Text('Backend: ${stream.backendName}'),
                Text('Sample rate: ${stream.sampleRate} Hz'),
                Text('Frames per callback: ${stream.framesPerCallback}'),
              ],
              if (stats != null) ...[
                Text('Callbacks: ${stats.callbacks}'),
                Text(
                  'Period mean/min/max: '
                  '${(stats.periodMeanNs / 1e6).toStringAsFixed(2)} / '
                  '${(stats.periodMinNs / 1e6).toStringAsFixed(2)} / '
                  '${(stats.periodMaxNs / 1e6).toStringAsFixed(2)} ms',
                ),
                Text('Late callbacks: ${stats.lateCallbacks}'),
                Text('Xruns: ${stats.xruns}'),
              ],
            ],
          ),
        ),
      ),
    );
  }
}
