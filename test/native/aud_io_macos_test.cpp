// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The native tests of the macOS backend, aud_io_macos.mm: what it decides
// about the devices of the HAL - their rates and routes, and when a stream
// reopens -, which lives in hal:: of aud_io_internal.hpp, so that it runs
// without the HAL on every platform.

#include <cstdint>
#include <utility>
#include <vector>

#include "aud_audio_io.h"
#include "aud_io_internal.hpp"
#include "aud_test.hpp"

namespace {

namespace hal = aud_io::hal;

// The presence of a device; lossOf reads it whichever property changed.
constexpr uint32_t kDeviceIsAlive = hal::fourCc("livn");

// What a stream asks for that names no device and follows the hardware.
aud_io::DeviceRequest anyDevice(uint32_t direction) {
  aud_io::DeviceRequest request;
  request.direction = direction;
  return request;
}

// What the stream got: 48 kHz and two channels in each of its directions.
aud_io::DeviceFormat streamFormat(uint32_t direction) {
  aud_io::DeviceFormat format;
  format.sampleRate = 48000;
  format.outputChannels = (direction & AUD_IO_OUTPUT) != 0 ? 2 : 0;
  format.inputChannels = (direction & AUD_IO_INPUT) != 0 ? 2 : 0;
  return format;
}

// A device the stream uses in `direction`, opened while it ran at `rate`.
hal::DeviceUse useOf(uint32_t direction, double rate = 48000) {
  hal::DeviceUse use;
  use.output = (direction & AUD_IO_OUTPUT) != 0;
  use.input = (direction & AUD_IO_INPUT) != 0;
  use.rate = rate;
  return use;
}

// A present device at `rate` with `outputs` and `inputs` channels.
hal::DeviceProperties present(double rate, uint32_t outputs,
                              uint32_t inputs) {
  hal::DeviceProperties properties;
  properties.rate = rate;
  properties.outputs = outputs;
  properties.inputs = inputs;
  return properties;
}

}  // namespace

// ############################################################################
// Codes, rates and routes

// A four-character code reads as the HAL writes it, 'usb ' as 0x75736220.
AUD_TEST(hal_four_character_codes_read_as_the_hal_writes_them) {
  AUD_CHECK(hal::fourCc("usb ") == 0x75736220u);
  AUD_CHECK(hal::kTransportFireWire == 0x31333934u);
}

// The rates are the ends of the ranges, each once and sorted: a discrete
// rate is a range of one rate, a continuous range lists its ends.
AUD_TEST(hal_rates_are_the_ends_of_the_ranges_once_and_sorted) {
  const std::vector<double> discrete = {44100, 48000, 96000};
  AUD_CHECK(hal::ratesOf({{96000, 96000}, {44100, 44100}, {48000, 48000}}) ==
            discrete);
  const std::vector<double> continuous = {8000, 44100, 48000, 192000};
  AUD_CHECK(hal::ratesOf({{48000, 48000}, {8000, 192000}, {44100, 48000}}) ==
            continuous);
  AUD_CHECK(hal::ratesOf({}).empty());
}

// A device with more rates than AudIoDevice holds lists the first
// AUD_IO_MAX_SAMPLE_RATES it names, sorted.
AUD_TEST(hal_rates_stop_at_what_the_device_record_holds) {
  std::vector<std::pair<double, double>> ranges;
  std::vector<double> first;
  for (int i = 0; i < AUD_IO_MAX_SAMPLE_RATES + 4; ++i) {
    const double rate = 192000 - 8000 * i;
    ranges.emplace_back(rate, rate);
    if (i < AUD_IO_MAX_SAMPLE_RATES) first.insert(first.begin(), rate);
  }
  AUD_CHECK(hal::ratesOf(ranges) == first);
}

// Every transport type maps to the route of its output and its input.
AUD_TEST(hal_routes_follow_the_transport_type) {
  using Routes = std::vector<std::pair<uint32_t, uint32_t>>;
  const std::vector<uint32_t> transports = {
      hal::kTransportBuiltIn,     hal::kTransportUsb,
      hal::kTransportBluetooth,   hal::kTransportBluetoothLe,
      hal::kTransportHdmi,        hal::kTransportDisplayPort,
      hal::kTransportAirPlay,     hal::kTransportVirtual,
      hal::kTransportAggregate,   hal::kTransportFireWire,
      hal::kTransportPci,         hal::kTransportThunderbolt,
      hal::kTransportAvb,         0,
      hal::fourCc("ccwd"),  // Continuity Capture
  };
  Routes routes;
  for (const uint32_t transport : transports) {
    routes.emplace_back(hal::routeOf(transport, true, 0),
                        hal::routeOf(transport, false, 0));
  }
  const Routes expected = {
      {AUD_IO_ROUTE_SPEAKER, AUD_IO_ROUTE_BUILTIN_MIC},
      {AUD_IO_ROUTE_USB, AUD_IO_ROUTE_USB},
      {AUD_IO_ROUTE_BLUETOOTH_A2DP, AUD_IO_ROUTE_BLUETOOTH_HFP},
      {AUD_IO_ROUTE_BLUETOOTH_LE, AUD_IO_ROUTE_BLUETOOTH_LE},
      {AUD_IO_ROUTE_HDMI, AUD_IO_ROUTE_HDMI},
      {AUD_IO_ROUTE_HDMI, AUD_IO_ROUTE_HDMI},
      {AUD_IO_ROUTE_AIRPLAY, AUD_IO_ROUTE_AIRPLAY},
      {AUD_IO_ROUTE_VIRTUAL, AUD_IO_ROUTE_VIRTUAL},
      {AUD_IO_ROUTE_VIRTUAL, AUD_IO_ROUTE_VIRTUAL},
      {AUD_IO_ROUTE_LINE, AUD_IO_ROUTE_LINE},
      {AUD_IO_ROUTE_LINE, AUD_IO_ROUTE_LINE},
      {AUD_IO_ROUTE_LINE, AUD_IO_ROUTE_LINE},
      {AUD_IO_ROUTE_LINE, AUD_IO_ROUTE_LINE},
      {AUD_IO_ROUTE_UNKNOWN, AUD_IO_ROUTE_UNKNOWN},
      {AUD_IO_ROUTE_UNKNOWN, AUD_IO_ROUTE_UNKNOWN},
  };
  AUD_CHECK(routes == expected);
}

// The data source of a built-in output tells headphones in the jack from
// the speaker; the microphone and other transports ignore it.
AUD_TEST(hal_a_built_in_output_tells_headphones_by_its_data_source) {
  AUD_CHECK(hal::routeOf(hal::kTransportBuiltIn, true,
                         hal::kSourceHeadphones) ==
            AUD_IO_ROUTE_WIRED_HEADPHONES);
  AUD_CHECK(hal::routeOf(hal::kTransportBuiltIn, true, hal::fourCc("ispk")) ==
            AUD_IO_ROUTE_SPEAKER);
  AUD_CHECK(hal::routeOf(hal::kTransportBuiltIn, false,
                         hal::kSourceHeadphones) == AUD_IO_ROUTE_BUILTIN_MIC);
  AUD_CHECK(hal::routeOf(hal::kTransportUsb, true, hal::kSourceHeadphones) ==
            AUD_IO_ROUTE_USB);
}

// ############################################################################
// Devices that come and go

// A stream loses a device of either direction that the device list no
// longer holds; a direction it lacks (0) is never missing.
AUD_TEST(hal_a_device_missing_from_the_list_is_lost) {
  const std::vector<uint32_t> present = {10, 11};
  AUD_CHECK(!hal::missingFrom(present, 10, 0));
  AUD_CHECK(!hal::missingFrom(present, 10, 11));
  AUD_CHECK(!hal::missingFrom(present, 0, 0));
  AUD_CHECK(hal::missingFrom(present, 12, 0));
  AUD_CHECK(hal::missingFrom(present, 0, 12));
  AUD_CHECK(hal::missingFrom(present, 10, 12));
  AUD_CHECK(hal::missingFrom({}, 10, 0));
}

// A stream that named no device for a direction it has follows the default
// of that direction; one that named its device stays on it.
AUD_TEST(hal_a_stream_without_an_id_moves_to_the_new_default) {
  const aud_io::DeviceRequest output = anyDevice(AUD_IO_OUTPUT);
  const aud_io::DeviceFormat outputOnly = streamFormat(AUD_IO_OUTPUT);
  AUD_CHECK(hal::movesToDefault(true, output, outputOnly, 10, 11));
  // It runs on the default already, or there is none.
  AUD_CHECK(!hal::movesToDefault(true, output, outputOnly, 11, 11));
  AUD_CHECK(!hal::movesToDefault(true, output, outputOnly, 10, 0));
  // A stream without input ignores the default input.
  AUD_CHECK(!hal::movesToDefault(false, output, outputOnly, 0, 20));
  // A duplex stream on a named output follows the default input alone.
  aud_io::DeviceRequest named = anyDevice(AUD_IO_DUPLEX);
  named.outputId = "usb";
  const aud_io::DeviceFormat duplex = streamFormat(AUD_IO_DUPLEX);
  AUD_CHECK(!hal::movesToDefault(true, named, duplex, 10, 11));
  AUD_CHECK(hal::movesToDefault(false, named, duplex, 20, 21));
}

// A device that went away removes the stream's device, whichever of its
// listeners tells of it, and so does one that lost a direction the stream
// uses.
AUD_TEST(hal_a_device_that_went_away_is_removed) {
  const aud_io::DeviceRequest request = anyDevice(AUD_IO_DUPLEX);
  const aud_io::DeviceFormat format = streamFormat(AUD_IO_DUPLEX);
  hal::DeviceProperties gone = present(48000, 2, 2);
  gone.alive = false;
  for (const uint32_t property :
       {kDeviceIsAlive, hal::kNominalSampleRate, hal::kStreamConfiguration}) {
    AUD_CHECK(hal::lossOf(property, gone, useOf(AUD_IO_DUPLEX), request,
                          format) == AUD_IO_REASON_DEVICE_REMOVED);
  }
  AUD_CHECK(hal::lossOf(hal::kStreamConfiguration, present(48000, 0, 2),
                        useOf(AUD_IO_OUTPUT), request,
                        format) == AUD_IO_REASON_DEVICE_REMOVED);
  AUD_CHECK(hal::lossOf(hal::kStreamConfiguration, present(48000, 2, 0),
                        useOf(AUD_IO_INPUT), request,
                        format) == AUD_IO_REASON_DEVICE_REMOVED);
  // The input it lost is the one of another device of the stream.
  AUD_CHECK(hal::lossOf(hal::kStreamConfiguration, present(48000, 2, 0),
                        useOf(AUD_IO_OUTPUT), request,
                        format) == AUD_IO_REASON_NONE);
}

// A stream that follows the hardware reopens for a new channel count, at
// most two counting, as on iOS; one that asked for its channels keeps
// them, miniaudio converts.
AUD_TEST(hal_a_stream_that_follows_the_hardware_reopens_for_new_channels) {
  const aud_io::DeviceRequest follows = anyDevice(AUD_IO_DUPLEX);
  aud_io::DeviceFormat format = streamFormat(AUD_IO_DUPLEX);
  format.inputChannels = 1;  // a mono microphone
  const hal::DeviceUse both = useOf(AUD_IO_DUPLEX);
  AUD_CHECK(hal::lossOf(hal::kStreamConfiguration, present(48000, 6, 1), both,
                        follows, format) == AUD_IO_REASON_NONE);
  AUD_CHECK(hal::lossOf(hal::kStreamConfiguration, present(48000, 1, 1), both,
                        follows, format) == AUD_IO_REASON_DEVICE_REMOVED);
  AUD_CHECK(hal::lossOf(hal::kStreamConfiguration, present(48000, 2, 2), both,
                        follows, format) == AUD_IO_REASON_DEVICE_REMOVED);
  aud_io::DeviceRequest fixed = follows;
  fixed.outputChannels = 2;
  fixed.inputChannels = 1;
  AUD_CHECK(hal::lossOf(hal::kStreamConfiguration, present(48000, 1, 2), both,
                        fixed, format) == AUD_IO_REASON_NONE);
}

// Only the rate and the stream configuration weigh what they read: the
// presence, the data source or an overload of a present device reopen
// nothing, though its rate and channels differ.
AUD_TEST(hal_other_properties_of_a_present_device_reopen_nothing) {
  const aud_io::DeviceRequest follows = anyDevice(AUD_IO_OUTPUT);
  const aud_io::DeviceFormat format = streamFormat(AUD_IO_OUTPUT);
  for (const uint32_t property :
       {kDeviceIsAlive, hal::fourCc("ssrc"), hal::fourCc("over")}) {
    AUD_CHECK(hal::lossOf(property, present(44100, 1, 0), useOf(AUD_IO_OUTPUT),
                          follows, format) == AUD_IO_REASON_NONE);
  }
}

// ############################################################################
// Rates

// Only a stream that asked for no rate runs at the device's and reopens
// when that rate changes; miniaudio converts for a stream that asked for
// one, so that a 44.1 kHz stream on a 48 kHz device no longer reopens on
// every rate notification.
AUD_TEST(hal_only_a_stream_that_asked_for_no_rate_reopens_for_a_rate) {
  const aud_io::DeviceRequest follows = anyDevice(AUD_IO_OUTPUT);
  AUD_CHECK(hal::reopensForRate(follows, 48000, 44100));
  AUD_CHECK(!hal::reopensForRate(follows, 48000, 48000));
  AUD_CHECK(!hal::reopensForRate(follows, 48000, 48000.25));
  AUD_CHECK(!hal::reopensForRate(follows, 48000, 0));  // unknown
  AUD_CHECK(hal::reopensForRate(follows, 0, 48000));   // known only now
  aud_io::DeviceRequest fixed = follows;
  fixed.sampleRate = 44100;
  for (const double rate : {44100.0, 48000.0, 96000.0}) {
    AUD_CHECK(!hal::reopensForRate(fixed, 48000, rate));
  }
  aud_io::DeviceFormat format = streamFormat(AUD_IO_OUTPUT);
  format.sampleRate = 44100;
  AUD_CHECK(hal::lossOf(hal::kNominalSampleRate, present(48000, 2, 0),
                        useOf(AUD_IO_OUTPUT), fixed,
                        format) == AUD_IO_REASON_NONE);
}

// A duplex stream runs at the rate of its output; each of its devices is
// weighed by its own rate when the stream opened it, so that an input at
// 44.1 kHz does not reopen the stream for differing from the output.
AUD_TEST(hal_a_duplex_stream_weighs_each_device_by_its_own_rate) {
  const aud_io::DeviceRequest request = anyDevice(AUD_IO_DUPLEX);
  const aud_io::DeviceFormat format = streamFormat(AUD_IO_DUPLEX);
  const hal::DeviceUse output = useOf(AUD_IO_OUTPUT, 48000);
  const hal::DeviceUse input = useOf(AUD_IO_INPUT, 44100);
  AUD_CHECK(hal::lossOf(hal::kNominalSampleRate, present(44100, 0, 2), input,
                        request, format) == AUD_IO_REASON_NONE);
  AUD_CHECK(hal::lossOf(hal::kNominalSampleRate, present(48000, 0, 2), input,
                        request, format) == AUD_IO_REASON_SAMPLE_RATE);
  AUD_CHECK(hal::lossOf(hal::kNominalSampleRate, present(48000, 2, 0), output,
                        request, format) == AUD_IO_REASON_NONE);
  AUD_CHECK(hal::lossOf(hal::kNominalSampleRate, present(96000, 2, 0), output,
                        request, format) == AUD_IO_REASON_SAMPLE_RATE);
}

// ############################################################################
// Reports

// A device reports one loss, the first; no loss is none to report.
AUD_TEST(hal_a_device_reports_its_loss_once) {
  hal::LossReport report;
  AUD_CHECK(!report.take(AUD_IO_REASON_NONE));
  AUD_CHECK(report.take(AUD_IO_REASON_SAMPLE_RATE));
  AUD_CHECK(!report.take(AUD_IO_REASON_SAMPLE_RATE));
  AUD_CHECK(!report.take(AUD_IO_REASON_DEVICE_REMOVED));
  AUD_CHECK(report.reason == AUD_IO_REASON_SAMPLE_RATE);
}

// Unplugging the default output reaches the backend three times - the
// device list, the default output and the device's presence -; the stream
// hears of it once, with the reason of the first, and does not close the
// device it reopened meanwhile.
AUD_TEST(hal_one_unplug_of_the_default_output_reaches_the_stream_once) {
  const uint32_t unplugged = 10;
  const uint32_t speaker = 11;
  const aud_io::DeviceRequest request = anyDevice(AUD_IO_OUTPUT);
  const aud_io::DeviceFormat format = streamFormat(AUD_IO_OUTPUT);
  hal::DeviceProperties gone;
  gone.alive = false;
  std::vector<uint32_t> losses;
  if (hal::missingFrom({speaker}, unplugged, 0)) {
    losses.push_back(AUD_IO_REASON_DEVICE_REMOVED);
  }
  if (hal::movesToDefault(true, request, format, unplugged, speaker)) {
    losses.push_back(AUD_IO_REASON_ROUTE_OVERRIDE);
  }
  losses.push_back(hal::lossOf(kDeviceIsAlive, gone, useOf(AUD_IO_OUTPUT),
                               request, format));
  hal::LossReport report;
  std::vector<uint32_t> reported;
  for (const uint32_t loss : losses) {
    if (report.take(loss)) reported.push_back(loss);
  }
  const std::vector<uint32_t> each = {AUD_IO_REASON_DEVICE_REMOVED,
                                      AUD_IO_REASON_ROUTE_OVERRIDE,
                                      AUD_IO_REASON_DEVICE_REMOVED};
  const std::vector<uint32_t> once = {AUD_IO_REASON_DEVICE_REMOVED};
  AUD_CHECK(losses == each);
  AUD_CHECK(reported == once);
}
