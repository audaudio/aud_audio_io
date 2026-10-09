// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:async';
import 'dart:convert';

import 'package:aud_audio_io/aud_audio_io.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  runApp(const AudIoExampleApp());
}

// #############################################################################
/// What the example renders.
enum AudIoExampleMode {
  /// A sine on the output.
  sine('Sine', AudIoDirection.output),

  /// The input on the output.
  monitor('Monitor', AudIoDirection.duplex),

  /// Clicks on the output, found again at the input.
  probe('Latency probe', AudIoDirection.duplex);

  const AudIoExampleMode(this.label, this.direction);

  /// The name on the button.
  final String label;

  /// The direction of the stream.
  final AudIoDirection direction;
}

// #############################################################################
/// Shows the devices, plays a stream in one of [AudIoExampleMode] and shows
/// its format, counters, timing and notifications: the app the numbers on
/// the reference devices are measured with (ticket 21).
class AudIoExampleApp extends StatefulWidget {
  /// Creates the example app.
  const AudIoExampleApp({super.key});

  @override
  State<AudIoExampleApp> createState() => _AudIoExampleAppState();
}

class _AudIoExampleAppState extends State<AudIoExampleApp> {
  late final AudIoSession _session;
  late final StreamSubscription<AudIoNotification> _subscription;
  List<AudIoDevice> _devices = const [];
  AudIoPermission? _permission;
  AudIoExampleMode _mode = AudIoExampleMode.sine;
  AudIoStream? _stream;
  AudIoProbe? _probe;
  AudIoStreamFormat? _format;
  AudIoCounters? _counters;
  AudIoProbeResult? _probeResult;
  Timer? _timer;
  String? _error;
  final List<String> _log = [];

  @override
  void initState() {
    super.initState();
    _session = AudIoSession(directions: AudIoDirection.duplex);
    _subscription = _session.notifications.listen(_onNotification);
    _refresh();
  }

  @override
  void dispose() {
    _timer?.cancel();
    unawaited(_subscription.cancel());
    _session.dispose();
    _probe?.dispose();
    super.dispose();
  }

  void _refresh() {
    try {
      _devices = _session.devices;
      _permission = _session.permission;
    } on AudIoException catch (e) {
      _error = e.message;
    }
  }

  void _onNotification(AudIoNotification notification) {
    final stream = _stream;
    if (notification.type == AudIoNotificationType.devicesChanged) _refresh();
    setState(() {
      _format = stream == null || stream.isClosed ? null : stream.format;
      _log.insert(
        0,
        '${notification.type.name} ${notification.reason.name}'
        '${notification.value > 0 ? ' ${notification.elapsed.inMilliseconds} ms' : ''}',
      );
      if (_log.length > 30) _log.removeLast();
    });
  }

  Future<void> _askPermission() async {
    final answer = await _session.requestPermission();
    setState(() => _permission = answer);
  }

  void _start() {
    setState(() => _error = null);
    try {
      final probe = _mode == AudIoExampleMode.probe
          ? AudIoProbe(intervalFrames: 24000)
          : null;
      final stream = _session.open(
        // The render functions of the package follow a new format.
        AudIoStreamConfig(direction: _mode.direction, followFormat: true),
        render: switch (_mode) {
          AudIoExampleMode.sine => AudIoStream.sineRender,
          AudIoExampleMode.monitor => AudIoStream.thruRender,
          AudIoExampleMode.probe => AudIoProbe.render,
        },
        user: probe?.pointer,
      );
      stream.start();
      _probe = probe;
      _stream = stream;
      _format = stream.format;
      _timer = Timer.periodic(const Duration(milliseconds: 250), (_) {
        if (stream.isClosed) return;
        setState(() {
          _counters = stream.counters;
          _probeResult = _probe?.read();
        });
      });
    } on AudIoException catch (e) {
      setState(() => _error = e.message);
    }
  }

  void _stop() {
    _timer?.cancel();
    _stream?.close();
    _probe?.dispose();
    setState(() {
      _stream = null;
      _probe = null;
    });
  }

  Map<String, Object?> _report() {
    final format = _format;
    final counters = _counters;
    final probe = _probeResult;
    double ms(int ns) => ns / 1e6;
    return {
      'mode': _mode.name,
      if (format != null)
        'format': {
          'backend': format.backend,
          'sampleRate': format.sampleRate,
          'outputChannels': format.outputChannels,
          'inputChannels': format.inputChannels,
          'bufferFrames': format.bufferFrames,
          'burstFrames': format.burstFrames,
          'maxFrames': format.maxFrames,
          'exclusive': format.exclusive,
          'timeSource': format.timeSource.name,
        },
      if (counters != null)
        'counters': {
          'callbacks': counters.callbacks,
          'callbackFramesMin': counters.callbackFramesMin,
          'callbackFramesMax': counters.callbackFramesMax,
          'periodMeanMs': ms(counters.periodMeanNs.round()),
          'periodMaxMs': ms(counters.periodMaxNs),
          'lateCallbacks': counters.lateCallbacks,
          'xruns': counters.xruns,
          'disconnects': counters.disconnects,
          'recoveries': counters.recoveries,
          'recoveryLastMs': ms(counters.recoveryTimeLastNs),
          'recoveryMaxMs': ms(counters.recoveryTimeMaxNs),
          'callbackMeanMs': ms(counters.callbackTimeMeanNs.round()),
          'callbackMaxMs': ms(counters.callbackTimeMaxNs),
          'jitterMaxMs': ms(counters.hostTimeJitterMaxNs),
          'accuracyMs': ms(counters.lastTime.hostTimeAccuracyNs),
          'outputLatencyFrames': counters.lastTime.outputLatencyFrames,
          'inputLatencyFrames': counters.lastTime.inputLatencyFrames,
        },
      if (probe != null)
        'probe': {
          'clicks': probe.clicks,
          'detections': probe.detections,
          'roundTripFrames': probe.lastRoundTripFrames,
          'roundTripMinFrames': probe.minRoundTripFrames,
          'roundTripMaxFrames': probe.maxRoundTripFrames,
          'reportedFrames': probe.reportedRoundTripFrames,
          'errorFrames': probe.errorFrames,
          'inputPeak': probe.inputPeak,
        },
    };
  }

  @override
  Widget build(BuildContext context) {
    final running = _stream != null;
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('aud_audio_io')),
        body: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            Text('Backend: ${_session.backendName}'),
            Row(
              children: [
                Text('Microphone: ${_permission?.name ?? '?'}'),
                const SizedBox(width: 8),
                if (_permission != AudIoPermission.granted)
                  TextButton(
                    onPressed: _askPermission,
                    child: const Text('Allow'),
                  ),
              ],
            ),
            const SizedBox(height: 8),
            SegmentedButton<AudIoExampleMode>(
              segments: [
                for (final mode in AudIoExampleMode.values)
                  ButtonSegment(value: mode, label: Text(mode.label)),
              ],
              selected: {_mode},
              onSelectionChanged: running
                  ? null
                  : (selection) => setState(() => _mode = selection.single),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                FilledButton(
                  key: const Key('start'),
                  onPressed: running ? _stop : _start,
                  child: Text(running ? 'Stop' : 'Start'),
                ),
                const SizedBox(width: 8),
                OutlinedButton(
                  onPressed: () => Clipboard.setData(
                    ClipboardData(
                      text: const JsonEncoder.withIndent('  ')
                          .convert(_report()),
                    ),
                  ),
                  child: const Text('Copy report'),
                ),
              ],
            ),
            if (_error != null)
              Text(_error!, style: const TextStyle(color: Colors.red)),
            _Section('Format', [
              if (_format case final format?) ...[
                format.toString(),
                'Time source: ${format.timeSource.name}',
                'Devices: out "${format.outputDeviceId}" '
                    'in "${format.inputDeviceId}"',
              ],
            ]),
            _Section('Counters', [
              if (_counters case final c?) ...[
                'State: ${c.state.name}',
                'Callbacks: ${c.callbacks} '
                    '(${c.callbackFramesMin}..${c.callbackFramesMax} frames)',
                'Period: ${(c.periodMeanNs / 1e6).toStringAsFixed(2)} ms '
                    'mean, ${(c.periodMaxNs / 1e6).toStringAsFixed(2)} max',
                'Late: ${c.lateCallbacks}, xruns: ${c.xruns}',
                'Disconnects: ${c.disconnects}, recoveries: ${c.recoveries} '
                    '(last ${(c.recoveryTimeLastNs / 1e6).toStringAsFixed(0)} ms)',
                'Interruptions: ${c.interruptions}, held: ${c.heldBlocks}',
                'Callback: ${(c.callbackTimeMeanNs / 1e3).toStringAsFixed(0)} '
                    'µs mean, ${(c.callbackTimeMaxNs / 1e3).toStringAsFixed(0)}'
                    ' µs max',
                'Latency: out ${c.lastTime.outputLatencyFrames}, '
                    'in ${c.lastTime.inputLatencyFrames} frames',
                'Host time: ${c.lastTime.hostTimeSource.name}, accuracy '
                    '${(c.lastTime.hostTimeAccuracyNs / 1e3).toStringAsFixed(0)}'
                    ' µs, jitter max '
                    '${(c.hostTimeJitterMaxNs / 1e3).toStringAsFixed(0)} µs',
              ],
            ]),
            if (_probeResult case final p?)
              _Section('Latency probe', [
                'Clicks: ${p.clicks}, found: ${p.detections}',
                'Round trip: ${p.lastRoundTripFrames} frames '
                    '(${p.minRoundTripFrames}..${p.maxRoundTripFrames})',
                'Reported: ${p.reportedRoundTripFrames} frames, '
                    'error ${p.errorFrames} frames',
                'Input peak: ${p.inputPeak.toStringAsFixed(3)}',
              ]),
            _Section('Devices', [
              for (final d in _devices)
                '${d.isDefaultOutput || d.isDefaultInput ? '* ' : ''}'
                    '${d.name} (${d.route.name}, ${d.directions.name}, '
                    'id ${d.id})',
            ]),
            _Section('Notifications', _log),
          ],
        ),
      ),
    );
  }
}

class _Section extends StatelessWidget {
  const _Section(this.title, this.lines);

  final String title;
  final List<String> lines;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(top: 16),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(title, style: Theme.of(context).textTheme.titleMedium),
        for (final line in lines) Text(line),
      ],
    ),
  );
}
