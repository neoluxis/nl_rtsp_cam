// 实现 RTSP 参数验证、NV12 布局整理和相机时间换算。
#include "nl_rtsp_cam/frame_utils.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace nl::rtsp_cam {
namespace {
constexpr size_t kHbmMsg1080PMaxSize = 6220800;
}

std::string validate_config(const Config& config) {
  if (config.rtsp_url.empty())
    return "rtsp_url is required";
  if (config.rtsp_url.rfind("rtsp://", 0) != 0 && config.rtsp_url.rfind("rtsps://", 0) != 0)
    return "rtsp_url must be RTSP";
  if (config.transport != "tcp" && config.transport != "udp")
    return "invalid transport";
  if (config.pixel_format != "nv12" && config.pixel_format != "jpeg")
    return "invalid pixel_format";
  if (config.timestamp_source != "receive" && config.timestamp_source != "camera")
    return "invalid timestamp_source";
  if (config.frame_id.empty())
    return "frame_id is required";
  if (config.node_id.empty() || config.camera_id.empty())
    return "node_id and camera_id are required";
  if (config.connect_timeout_ms <= 0 || config.read_timeout_ms <= 0 ||
      config.reconnect_delay_ms < 0 || config.framerate < 0)
    return "invalid timeout or framerate";
  if (config.bitstream_buffer_bytes < 1024 * 1024 ||
      config.bitstream_buffer_bytes > 64 * 1024 * 1024)
    return "invalid bitstream_buffer_bytes";
  return {};
}

PublicationMode publication_mode(const Config& config) noexcept {
  if (config.zero_copy)
    return PublicationMode::kHbmem;
  return config.pixel_format == "jpeg" ? PublicationMode::kRosJpeg : PublicationMode::kRosImage;
}

void configure_rtsp_options(const Config& config, AVDictionary** options) {
  av_dict_set(options, "rtsp_transport", config.transport.c_str(), 0);
  av_dict_set_int(options, "stimeout", static_cast<int64_t>(config.connect_timeout_ms) * 1000, 0);
}

bool contains_keyframe(VideoCodec codec, std::span<const uint8_t> packet) noexcept {
  if (codec == VideoCodec::kMjpeg)
    return !packet.empty();
  for (size_t i = 0; i + 3 < packet.size(); ++i) {
    if (packet[i] != 0 || packet[i + 1] != 0)
      continue;
    size_t start = 0;
    if (packet[i + 2] == 1)
      start = i + 3;
    if (i + 4 < packet.size() && packet[i + 2] == 0 && packet[i + 3] == 1)
      start = i + 4;
    if (start == 0 || start >= packet.size())
      continue;
    const uint8_t header = packet[start];
    if (codec == VideoCodec::kH264 && (header & 0x1f) == 5)
      return true;
    if (codec == VideoCodec::kH265) {
      const uint8_t type = (header >> 1) & 0x3f;
      if (type >= 16 && type <= 21)
        return true;
    }
  }
  return false;
}

bool fits_hbmem(int width, int height) noexcept {
  if (width <= 0 || height <= 0 || width % 2 != 0 || height % 2 != 0)
    return false;
  return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 3 / 2 <=
         kHbmMsg1080PMaxSize;
}

bool copy_nv12(const uint8_t* y,
               const uint8_t* uv,
               int width,
               int height,
               int y_stride,
               int uv_stride,
               std::vector<uint8_t>& output) {
  if (y == nullptr || uv == nullptr || width <= 0 || height <= 0 || width % 2 != 0 ||
      height % 2 != 0 || y_stride < width || uv_stride < width || !fits_hbmem(width, height))
    return false;
  const size_t plane_size = static_cast<size_t>(width) * height;
  output.resize(plane_size * 3 / 2);
  for (int row = 0; row < height; ++row) {
    std::memcpy(output.data() + static_cast<size_t>(row) * width,
                y + static_cast<size_t>(row) * y_stride,
                width);
  }
  for (int row = 0; row < height / 2; ++row) {
    std::memcpy(output.data() + plane_size + static_cast<size_t>(row) * width,
                uv + static_cast<size_t>(row) * uv_stride,
                width);
  }
  return true;
}

std::optional<int64_t> camera_stamp(int64_t realtime_base_us,
                                    int64_t pts,
                                    AVRational time_base) noexcept {
  if (realtime_base_us == AV_NOPTS_VALUE || pts == AV_NOPTS_VALUE || realtime_base_us <= 0 ||
      time_base.num <= 0 || time_base.den <= 0)
    return std::nullopt;
  const int64_t delta = av_rescale_q(pts, time_base, AVRational{1, 1000000});
  if (delta < -realtime_base_us)
    return std::nullopt;
  if (delta > 0 && realtime_base_us > std::numeric_limits<int64_t>::max() - delta)
    return std::nullopt;
  return realtime_base_us + delta;
}

bool CaptureState::enabled() const noexcept {
  return enabled_.load();
}

void CaptureState::set_enabled(bool enabled) noexcept {
  enabled_.store(enabled);
}

std::optional<std::chrono::milliseconds> reconnect_wait(
    const Config& config, bool stopping, const CaptureState& capture_state) noexcept {
  if (stopping || !capture_state.enabled())
    return std::nullopt;
  return std::chrono::milliseconds(config.reconnect_delay_ms);
}

bool KeyframeGate::accept(bool keyframe) noexcept {
  if (keyframe)
    started_ = true;
  return started_;
}

FrameRateLimiter::FrameRateLimiter(int max_fps) noexcept : max_fps_(max_fps) {}

bool FrameRateLimiter::allow(std::chrono::steady_clock::time_point now) noexcept {
  if (max_fps_ <= 0)
    return true;
  if (last_refill_) {
    const double elapsed = std::chrono::duration<double>(now - *last_refill_).count();
    if (elapsed > 0)
      tokens_ = std::min(2.0, tokens_ + elapsed * max_fps_);
  }
  last_refill_ = now;
  if (tokens_ < 1.0)
    return false;
  tokens_ -= 1.0;
  return true;
}

}  // namespace nl::rtsp_cam
