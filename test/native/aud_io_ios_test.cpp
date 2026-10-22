// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The native tests of the iOS backend, aud_io_ios.mm: what it decides
// about the routes of the AVAudioSession - why a route changed and when a
// device reopens -, which lives in ios:: of aud_io_internal.hpp, so that it
// runs without the session on every platform.

#include <cstdint>
#include <string>
#include <vector>

#include "aud_audio_io.h"
#include "aud_io_internal.hpp"
#include "aud_test.hpp"

namespace {

namespace ios = aud_io::ios;

// The other reasons of a route change (AVAudioSessionRouteChangeReason*).
constexpr uint64_t kUnknown = 0;
constexpr uint64_t kCategoryChange = 3;
constexpr uint64_t kOverride = 4;
constexpr uint64_t kWakeFromSleep = 6;
constexpr uint64_t kNoSuitableRouteForCategory = 7;
constexpr uint64_t kRouteConfigurationChange = 8;

// What a stream asks for that names no device and follows the hardware.
aud_io::DeviceRequest anyDevice(uint32_t direction) {
  aud_io::DeviceRequest request;
  request.direction = direction;
  return request;
}

// What a device got on the ports `output` and `input`, empty for a
// direction it lacks: two output channels and a mono microphone at `rate`.
aud_io::DeviceFormat deviceOn(const std::string& output,
                              const std::string& input, double rate = 48000) {
  aud_io::DeviceFormat format;
  format.sampleRate = rate;
  format.outputChannels = output.empty() ? 0 : 2;
  format.inputChannels = input.empty() ? 0 : 1;
  format.outputId = output;
  format.inputId = input;
  return format;
}

// The route on the ports `output` and `input` at `rate`, with the channels
// deviceOn gets there.
ios::Route routeOn(const std::string& output, const std::string& input,
                   double rate = 48000) {
  ios::Route route;
  route.rate = rate;
  route.outputs = 2;
  route.inputs = input.empty() ? 0 : 1;
  route.output = output;
  route.input = input;
  return route;
}

}  // namespace

// ############################################################################
// Route changes

// A route change tells of a device that went or came; every other reason
// overrides the route.
AUD_TEST(ios_a_route_change_tells_of_a_device_that_went_or_came) {
  std::vector<uint32_t> reasons;
  for (const uint64_t change :
       {ios::kOldDeviceUnavailable, ios::kNewDeviceAvailable, kUnknown,
        kCategoryChange, kOverride, kWakeFromSleep,
        kNoSuitableRouteForCategory, kRouteConfigurationChange}) {
    reasons.push_back(ios::reasonOf(change));
  }
  const std::vector<uint32_t> expected = {
      AUD_IO_REASON_DEVICE_REMOVED, AUD_IO_REASON_DEVICE_ADDED,
      AUD_IO_REASON_ROUTE_OVERRIDE, AUD_IO_REASON_ROUTE_OVERRIDE,
      AUD_IO_REASON_ROUTE_OVERRIDE, AUD_IO_REASON_ROUTE_OVERRIDE,
      AUD_IO_REASON_ROUTE_OVERRIDE, AUD_IO_REASON_ROUTE_OVERRIDE,
  };
  AUD_CHECK(reasons == expected);
}

// iOS moves the output itself: a device on the headphones keeps running
// when a new output comes or the route is overridden, and reopens once the
// headphones are gone.
AUD_TEST(ios_the_output_reopens_only_once_the_old_one_is_gone) {
  const aud_io::DeviceRequest request = anyDevice(AUD_IO_OUTPUT);
  const aud_io::DeviceFormat headphones = deviceOn("headphones", "");
  AUD_CHECK(ios::lossOf(AUD_IO_REASON_DEVICE_ADDED, routeOn("bluetooth", ""),
                        request, headphones) == AUD_IO_REASON_NONE);
  AUD_CHECK(ios::lossOf(AUD_IO_REASON_ROUTE_OVERRIDE, routeOn("speaker", ""),
                        request, headphones) == AUD_IO_REASON_NONE);
  AUD_CHECK(ios::lossOf(AUD_IO_REASON_DEVICE_REMOVED, routeOn("speaker", ""),
                        request, headphones) == AUD_IO_REASON_DEVICE_REMOVED);
  // Another device went; the headphones stay.
  AUD_CHECK(ios::lossOf(AUD_IO_REASON_DEVICE_REMOVED,
                        routeOn("headphones", ""), request,
                        headphones) == AUD_IO_REASON_NONE);
}

// A stream with input reopens on the new input, whatever moved it there.
AUD_TEST(ios_a_stream_with_input_reopens_on_the_new_input) {
  const aud_io::DeviceRequest request = anyDevice(AUD_IO_DUPLEX);
  const aud_io::DeviceFormat headset = deviceOn("headphones", "headset-mic");
  for (const uint32_t why :
       {AUD_IO_REASON_DEVICE_REMOVED, AUD_IO_REASON_DEVICE_ADDED,
        AUD_IO_REASON_ROUTE_OVERRIDE}) {
    AUD_CHECK(ios::lossOf(why, routeOn("headphones", "built-in-mic"), request,
                          headset) == why);
    AUD_CHECK(ios::lossOf(why, routeOn("headphones", "headset-mic"), request,
                          headset) == AUD_IO_REASON_NONE);
  }
}

// A stream that follows the hardware reopens for a new rate or a new
// channel count, at most two counting; one that asked for its rate and
// channels keeps them, miniaudio converts.
AUD_TEST(ios_a_stream_that_follows_the_hardware_reopens_for_a_new_format) {
  const aud_io::DeviceRequest follows = anyDevice(AUD_IO_DUPLEX);
  const aud_io::DeviceFormat format = deviceOn("speaker", "built-in-mic");
  const uint32_t why = AUD_IO_REASON_ROUTE_OVERRIDE;
  ios::Route same = routeOn("speaker", "built-in-mic");
  AUD_CHECK(ios::lossOf(why, same, follows, format) == AUD_IO_REASON_NONE);
  same.outputs = 6;
  AUD_CHECK(ios::lossOf(why, same, follows, format) == AUD_IO_REASON_NONE);
  const ios::Route rate = routeOn("speaker", "built-in-mic", 44100);
  ios::Route mono = routeOn("speaker", "built-in-mic");
  mono.outputs = 1;
  ios::Route stereoMic = routeOn("speaker", "built-in-mic");
  stereoMic.inputs = 2;
  aud_io::DeviceRequest fixed = follows;
  fixed.sampleRate = 48000;
  fixed.outputChannels = 2;
  fixed.inputChannels = 1;
  for (const ios::Route& route : {rate, mono, stereoMic}) {
    AUD_CHECK(ios::lossOf(why, route, follows, format) == why);
    AUD_CHECK(ios::lossOf(why, route, fixed, format) == AUD_IO_REASON_NONE);
  }
}

// ############################################################################
// Reports

// Unplugging a headset reaches the backend more than once while the device
// is still registered - the route change that tells of it, the change of
// the route's configuration that follows with the speaker's rate, the
// reset of the media services -, each a loss of the same device; the
// stream reopens it once (aud_io_test.cpp). The device it reopened on the
// speaker keeps running through the same route.
AUD_TEST(ios_one_unplug_of_a_headset_is_lost_more_than_once) {
  const aud_io::DeviceRequest request = anyDevice(AUD_IO_DUPLEX);
  const aud_io::DeviceFormat headset =
      deviceOn("headphones", "headset-mic", 44100);
  const ios::Route speaker = routeOn("speaker", "built-in-mic");
  const std::vector<uint32_t> losses = {
      ios::lossOf(ios::reasonOf(ios::kOldDeviceUnavailable), speaker, request,
                  headset),
      ios::lossOf(ios::reasonOf(kRouteConfigurationChange), speaker, request,
                  headset),
  };
  const std::vector<uint32_t> each = {AUD_IO_REASON_DEVICE_REMOVED,
                                      AUD_IO_REASON_ROUTE_OVERRIDE};
  AUD_CHECK(losses == each);
  AUD_CHECK(ios::lossOf(AUD_IO_REASON_ROUTE_OVERRIDE, speaker, request,
                        deviceOn("speaker", "built-in-mic")) ==
            AUD_IO_REASON_NONE);
}
