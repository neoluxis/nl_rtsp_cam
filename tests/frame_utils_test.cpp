// 验证 RTSP 图像布局、参数及时间戳的公共行为。
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavutil/dict.h>
}

#include "gtest/gtest.h"
#include "nl_rtsp_cam/frame_utils.hpp"

namespace nl::rtsp_cam {
namespace {

TEST(ConfigTest, RejectsMissingUrlAndInvalidModes) {
  Config config;
  EXPECT_FALSE(validate_config(config).empty());
  config.rtsp_url = "rtsp://camera/stream";
  EXPECT_TRUE(validate_config(config).empty());
  config.transport = "invalid";
  EXPECT_FALSE(validate_config(config).empty());
  config.transport = "tcp";
  config.pixel_format = "h264";
  EXPECT_FALSE(validate_config(config).empty());
  config.pixel_format = "nv12";
  config.timestamp_source = "invalid";
  EXPECT_FALSE(validate_config(config).empty());
  config.timestamp_source = "receive";
  config.camera_id.clear();
  EXPECT_EQ(validate_config(config), "node_id and camera_id are required");
  config.camera_id = "camera0";
  config.bitstream_buffer_bytes = 0;
  EXPECT_FALSE(validate_config(config).empty());
}

TEST(FrameTest, CopiesPaddedNv12PlanesWithoutPadding) {
  const std::array<uint8_t, 12> y{1, 2, 3, 4, 99, 99, 5, 6, 7, 8, 99, 99};
  const std::array<uint8_t, 6> uv{9, 10, 11, 12, 99, 99};
  std::vector<uint8_t> result;
  ASSERT_TRUE(copy_nv12(y.data(), uv.data(), 4, 2, 6, 6, result));
  EXPECT_EQ(result, (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
}

TEST(FrameTest, RejectsFramesExceedingMessageCapacity) {
  EXPECT_FALSE(fits_hbmem(3840, 2160));
  EXPECT_TRUE(fits_hbmem(1920, 1080));
  std::vector<uint8_t> result;
  EXPECT_FALSE(copy_nv12(nullptr, nullptr, 1920, 1080, 1920, 1920, result));
}

TEST(TimestampTest, UsesRtcpNtpOnlyWhenAbsoluteBaseExists) {
  constexpr int64_t kBase = 1'700'000'000'000'000;
  EXPECT_FALSE(camera_stamp(AV_NOPTS_VALUE, 90'000, AVRational{1, 90'000}).has_value());
  EXPECT_FALSE(camera_stamp(kBase, AV_NOPTS_VALUE, AVRational{1, 90'000}).has_value());
  EXPECT_EQ(camera_stamp(kBase, 90'000, AVRational{1, 90'000}), kBase + 1'000'000);
  EXPECT_FALSE(camera_stamp(1'000'000, -180'000, AVRational{1, 90'000}).has_value());
}

TEST(CaptureStateTest, StopAndRestartChangesDesiredConnectionState) {
  CaptureState state;
  EXPECT_TRUE(state.enabled());
  state.set_enabled(false);
  EXPECT_FALSE(state.enabled());
  state.set_enabled(true);
  EXPECT_TRUE(state.enabled());
}

TEST(PublicationTest, SelectsRosImageJpegAndHbmemTopics) {
  Config config;
  EXPECT_EQ(publication_mode(config), PublicationMode::kRosImage);
  config.pixel_format = "jpeg";
  EXPECT_EQ(publication_mode(config), PublicationMode::kRosJpeg);
  config.zero_copy = true;
  EXPECT_EQ(publication_mode(config), PublicationMode::kHbmem);
  config.pixel_format = "nv12";
  EXPECT_EQ(publication_mode(config), PublicationMode::kHbmem);
}

TEST(ReconnectTest, RetriesOnlyWhileCaptureIsEnabled) {
  Config config;
  config.reconnect_delay_ms = 250;
  CaptureState state;
  EXPECT_EQ(reconnect_wait(config, false, state), std::chrono::milliseconds(250));
  state.set_enabled(false);
  EXPECT_FALSE(reconnect_wait(config, false, state).has_value());
  state.set_enabled(true);
  EXPECT_EQ(reconnect_wait(config, false, state), std::chrono::milliseconds(250));
  EXPECT_FALSE(reconnect_wait(config, true, state).has_value());
}

TEST(ConfigTest, UsesClientSocketTimeoutInsteadOfListenTimeout) {
  Config config;
  config.rtsp_url = "rtsp://camera/stream";
  config.connect_timeout_ms = 1250;
  AVDictionary* options = nullptr;
  configure_rtsp_options(config, &options);
  const auto* transport = av_dict_get(options, "rtsp_transport", nullptr, 0);
  const auto* socket_timeout = av_dict_get(options, "stimeout", nullptr, 0);
  EXPECT_STREQ(transport->value, "tcp");
  EXPECT_STREQ(socket_timeout->value, "1250000");
  EXPECT_EQ(av_dict_get(options, "timeout", nullptr, 0), nullptr);
  av_dict_free(&options);
}

TEST(StreamTest, WaitsForKeyframeBeforeFeedingDecoder) {
  KeyframeGate gate;
  EXPECT_FALSE(gate.accept(false));
  EXPECT_FALSE(gate.accept(false));
  EXPECT_TRUE(gate.accept(true));
  EXPECT_TRUE(gate.accept(false));
}

TEST(StreamTest, DetectsAnnexBKeyframesWhenDemuxerDoesNotMarkThem) {
  const std::array<uint8_t, 5> h264_idr{0, 0, 0, 1, 0x65};
  const std::array<uint8_t, 5> h264_p{0, 0, 0, 1, 0x41};
  const std::array<uint8_t, 4> h264_short_start{0, 0, 1, 0x65};
  const std::array<uint8_t, 6> h265_idr{0, 0, 0, 1, 0x26, 0x01};
  EXPECT_TRUE(contains_keyframe(VideoCodec::kH264, h264_idr));
  EXPECT_FALSE(contains_keyframe(VideoCodec::kH264, h264_p));
  EXPECT_TRUE(contains_keyframe(VideoCodec::kH264, h264_short_start));
  EXPECT_TRUE(contains_keyframe(VideoCodec::kH265, h265_idr));
}

}  // namespace
}  // namespace nl::rtsp_cam
