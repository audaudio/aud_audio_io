// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The native tests of the Windows backend, aud_io_windows.cpp: what it
// decides about the endpoints of WASAPI - their routes, when a stream
// reopens, the period and the microphone permission -, which lives in
// wasapi:: of aud_io_internal.hpp, so that it runs without COM on every
// platform.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "aud_audio_io.h"
#include "aud_io_internal.hpp"
#include "aud_test.hpp"

namespace {

namespace wasapi = aud_io::wasapi;

// What a stream asks for that names no endpoint and follows the hardware.
aud_io::DeviceRequest anyDevice(uint32_t direction) {
  aud_io::DeviceRequest request;
  request.direction = direction;
  return request;
}

// What the stream got: 48 kHz and two channels in each of its directions,
// on the endpoints "out" and "in".
aud_io::DeviceFormat streamFormat(uint32_t direction) {
  aud_io::DeviceFormat format;
  format.sampleRate = 48000;
  if ((direction & AUD_IO_OUTPUT) != 0) {
    format.outputChannels = 2;
    format.outputId = "out";
  }
  if ((direction & AUD_IO_INPUT) != 0) {
    format.inputChannels = 2;
    format.inputId = "in";
  }
  return format;
}

// An active endpoint whose mix format runs at `rate` with `channels`.
wasapi::EndpointState active(double rate, uint32_t channels) {
  wasapi::EndpointState state;
  state.rate = rate;
  state.channels = channels;
  return state;
}

}  // namespace

// ############################################################################
// Buses and routes

// The bus follows the enumerator of the endpoint's device in any case: USB,
// the Bluetooth enumerators with LE Audio apart, and everything else.
AUD_TEST(wasapi_the_bus_follows_the_enumerator) {
  AUD_CHECK(wasapi::busOf("USB") == wasapi::Bus::usb);
  AUD_CHECK(wasapi::busOf("usb") == wasapi::Bus::usb);
  AUD_CHECK(wasapi::busOf("BTHENUM") == wasapi::Bus::bluetooth);
  AUD_CHECK(wasapi::busOf("BTHHFENUM") == wasapi::Bus::bluetooth);
  AUD_CHECK(wasapi::busOf("BTHLEDEVICE") == wasapi::Bus::bluetoothLe);
  AUD_CHECK(wasapi::busOf("HDAUDIO") == wasapi::Bus::other);
  AUD_CHECK(wasapi::busOf("MMDEVAPI") == wasapi::Bus::other);
  AUD_CHECK(wasapi::busOf("") == wasapi::Bus::other);
}

// On the other buses every form factor maps to the route of its output and
// its input.
AUD_TEST(wasapi_routes_follow_the_form_factor) {
  using Routes = std::vector<std::pair<uint32_t, uint32_t>>;
  const std::vector<uint32_t> formFactors = {
      wasapi::kRemoteNetworkDevice,
      wasapi::kSpeakers,
      wasapi::kLineLevel,
      wasapi::kHeadphones,
      wasapi::kMicrophone,
      wasapi::kHeadset,
      wasapi::kHandset,
      wasapi::kUnknownDigitalPassthrough,
      wasapi::kSpdif,
      wasapi::kDigitalAudioDisplayDevice,
      wasapi::kUnknownFormFactor,
      42,
  };
  Routes routes;
  for (const uint32_t formFactor : formFactors) {
    routes.emplace_back(
        wasapi::routeOf(formFactor, wasapi::Bus::other, true),
        wasapi::routeOf(formFactor, wasapi::Bus::other, false));
  }
  const Routes expected = {
      {AUD_IO_ROUTE_VIRTUAL, AUD_IO_ROUTE_VIRTUAL},
      {AUD_IO_ROUTE_SPEAKER, AUD_IO_ROUTE_SPEAKER},
      {AUD_IO_ROUTE_LINE, AUD_IO_ROUTE_LINE},
      {AUD_IO_ROUTE_WIRED_HEADPHONES, AUD_IO_ROUTE_WIRED_HEADPHONES},
      {AUD_IO_ROUTE_BUILTIN_MIC, AUD_IO_ROUTE_BUILTIN_MIC},
      {AUD_IO_ROUTE_WIRED_HEADSET, AUD_IO_ROUTE_WIRED_HEADSET},
      {AUD_IO_ROUTE_WIRED_HEADSET, AUD_IO_ROUTE_WIRED_HEADSET},
      {AUD_IO_ROUTE_LINE, AUD_IO_ROUTE_LINE},
      {AUD_IO_ROUTE_LINE, AUD_IO_ROUTE_LINE},
      {AUD_IO_ROUTE_HDMI, AUD_IO_ROUTE_HDMI},
      {AUD_IO_ROUTE_UNKNOWN, AUD_IO_ROUTE_UNKNOWN},
      {AUD_IO_ROUTE_UNKNOWN, AUD_IO_ROUTE_UNKNOWN},
  };
  AUD_CHECK(routes == expected);
}

// USB and Bluetooth win over the form factor; a Bluetooth headset and every
// Bluetooth input are hands-free, Bluetooth headphones play A2DP.
AUD_TEST(wasapi_usb_and_bluetooth_win_over_the_form_factor) {
  AUD_CHECK(wasapi::routeOf(wasapi::kSpeakers, wasapi::Bus::usb, true) ==
            AUD_IO_ROUTE_USB);
  AUD_CHECK(wasapi::routeOf(wasapi::kMicrophone, wasapi::Bus::usb, false) ==
            AUD_IO_ROUTE_USB);
  AUD_CHECK(wasapi::routeOf(wasapi::kHeadphones, wasapi::Bus::bluetooth,
                            true) == AUD_IO_ROUTE_BLUETOOTH_A2DP);
  AUD_CHECK(wasapi::routeOf(wasapi::kHeadset, wasapi::Bus::bluetooth, true) ==
            AUD_IO_ROUTE_BLUETOOTH_HFP);
  AUD_CHECK(wasapi::routeOf(wasapi::kHeadset, wasapi::Bus::bluetooth,
                            false) == AUD_IO_ROUTE_BLUETOOTH_HFP);
  AUD_CHECK(wasapi::routeOf(wasapi::kHeadphones, wasapi::Bus::bluetoothLe,
                            true) == AUD_IO_ROUTE_BLUETOOTH_LE);
}

// ############################################################################
// Endpoints that come, go and change

// A stream that named no endpoint for a direction it has follows the
// default of that direction; one that named its endpoint stays on it.
AUD_TEST(wasapi_a_stream_without_an_id_moves_to_the_new_default) {
  const aud_io::DeviceRequest output = anyDevice(AUD_IO_OUTPUT);
  const aud_io::DeviceFormat outputOnly = streamFormat(AUD_IO_OUTPUT);
  AUD_CHECK(wasapi::movesToDefault(true, output, outputOnly, "speakers"));
  // It runs on the default already, or there is none.
  AUD_CHECK(!wasapi::movesToDefault(true, output, outputOnly, "out"));
  AUD_CHECK(!wasapi::movesToDefault(true, output, outputOnly, ""));
  // A stream without input ignores the default input.
  AUD_CHECK(!wasapi::movesToDefault(false, output, outputOnly, "mic"));
  // A duplex stream on a named output follows the default input alone.
  aud_io::DeviceRequest named = anyDevice(AUD_IO_DUPLEX);
  named.outputId = "out";
  const aud_io::DeviceFormat duplex = streamFormat(AUD_IO_DUPLEX);
  AUD_CHECK(!wasapi::movesToDefault(true, named, duplex, "speakers"));
  AUD_CHECK(wasapi::movesToDefault(false, named, duplex, "mic"));
}

// An endpoint that is gone, unplugged or disabled removes the stream's
// device, in either direction.
AUD_TEST(wasapi_an_inactive_endpoint_is_removed) {
  const aud_io::DeviceRequest request = anyDevice(AUD_IO_DUPLEX);
  const aud_io::DeviceFormat format = streamFormat(AUD_IO_DUPLEX);
  wasapi::EndpointState gone = active(48000, 2);
  gone.active = false;
  AUD_CHECK(wasapi::lossOf(gone, true, 48000, request, format) ==
            AUD_IO_REASON_DEVICE_REMOVED);
  AUD_CHECK(wasapi::lossOf(gone, false, 48000, request, format) ==
            AUD_IO_REASON_DEVICE_REMOVED);
}

// A new rate of the mix format reopens a stream that follows the
// endpoint's rate; miniaudio converts for one that asked for a rate.
AUD_TEST(wasapi_a_new_mix_rate_reopens_a_stream_that_follows_it) {
  const aud_io::DeviceFormat format = streamFormat(AUD_IO_OUTPUT);
  const aud_io::DeviceRequest follows = anyDevice(AUD_IO_OUTPUT);
  AUD_CHECK(wasapi::lossOf(active(44100, 2), true, 48000, follows, format) ==
            AUD_IO_REASON_SAMPLE_RATE);
  aud_io::DeviceRequest fixed = anyDevice(AUD_IO_OUTPUT);
  fixed.sampleRate = 48000;
  AUD_CHECK(wasapi::lossOf(active(44100, 2), true, 48000, fixed, format) ==
            AUD_IO_REASON_NONE);
  // The rate it opened at, or one Windows does not tell, keeps it.
  AUD_CHECK(wasapi::lossOf(active(48000, 2), true, 48000, follows, format) ==
            AUD_IO_REASON_NONE);
  AUD_CHECK(wasapi::lossOf(active(0, 2), true, 48000, follows, format) ==
            AUD_IO_REASON_NONE);
}

// New channels of the mix format reopen a stream that follows the
// hardware's channels, up to the two a stream takes by default; one that
// asked for its channels keeps them.
AUD_TEST(wasapi_new_mix_channels_reopen_a_stream_that_follows_them) {
  const aud_io::DeviceFormat format = streamFormat(AUD_IO_DUPLEX);
  const aud_io::DeviceRequest follows = anyDevice(AUD_IO_DUPLEX);
  AUD_CHECK(wasapi::lossOf(active(48000, 1), true, 48000, follows, format) ==
            AUD_IO_REASON_DEVICE_REMOVED);
  AUD_CHECK(wasapi::lossOf(active(48000, 1), false, 48000, follows, format) ==
            AUD_IO_REASON_DEVICE_REMOVED);
  // Eight channels of an HDMI output still give two.
  AUD_CHECK(wasapi::lossOf(active(48000, 8), true, 48000, follows, format) ==
            AUD_IO_REASON_NONE);
  aud_io::DeviceRequest fixed = anyDevice(AUD_IO_DUPLEX);
  fixed.outputChannels = 2;
  AUD_CHECK(wasapi::lossOf(active(48000, 1), true, 48000, fixed, format) ==
            AUD_IO_REASON_NONE);
}

// ############################################################################
// The period and the permission

// The period rounds up to the engine's fundamental period within its
// minimum, so that miniaudio takes IAudioClient3; beyond the maximum, or
// without an engine that tells, the stream keeps the period it wants.
AUD_TEST(wasapi_the_period_rounds_up_to_the_fundamental_period) {
  // The inbox HDAudio driver: 128 to 480 frames in steps of 32 (48 kHz).
  AUD_CHECK(wasapi::periodOf(256, 32, 128, 480) == 256);
  AUD_CHECK(wasapi::periodOf(250, 32, 128, 480) == 256);
  AUD_CHECK(wasapi::periodOf(257, 32, 128, 480) == 288);
  AUD_CHECK(wasapi::periodOf(64, 32, 128, 480) == 128);
  AUD_CHECK(wasapi::periodOf(480, 32, 128, 480) == 480);
  AUD_CHECK(wasapi::periodOf(1024, 32, 128, 480) == 1024);
  AUD_CHECK(wasapi::periodOf(256, 0, 0, 0) == 256);
  // An engine with one period only.
  AUD_CHECK(wasapi::periodOf(256, 480, 480, 480) == 480);
}

// The microphone is granted unless one privacy setting denies it: for the
// device, for the user's apps or for desktop apps; an unset one allows.
AUD_TEST(wasapi_one_privacy_setting_denies_the_microphone) {
  AUD_CHECK(wasapi::permissionOf({"Allow", "Allow", "Allow"}) ==
            AUD_IO_PERMISSION_GRANTED);
  AUD_CHECK(wasapi::permissionOf({"", "", ""}) == AUD_IO_PERMISSION_GRANTED);
  AUD_CHECK(wasapi::permissionOf({}) == AUD_IO_PERMISSION_GRANTED);
  AUD_CHECK(wasapi::permissionOf({"Deny", "Allow", "Allow"}) ==
            AUD_IO_PERMISSION_DENIED);
  AUD_CHECK(wasapi::permissionOf({"Allow", "Allow", "Deny"}) ==
            AUD_IO_PERMISSION_DENIED);
}
