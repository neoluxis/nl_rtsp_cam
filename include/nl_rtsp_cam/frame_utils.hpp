// RTSP 节点的参数、图像布局和时间戳公共接口。
#ifndef NL_RTSP_CAM_FRAME_UTILS_HPP_
#define NL_RTSP_CAM_FRAME_UTILS_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/rational.h>
}

namespace nl::rtsp_cam {

/** RTSP 节点的启动参数。 */
struct Config {
  std::string rtsp_url;
  std::string transport = "tcp";
  std::string pixel_format = "nv12";
  std::string timestamp_source = "receive";
  std::string frame_id = "rtsp_cam";
  std::string camera_calibration_file_path;
  int connect_timeout_ms = 5000;
  int read_timeout_ms = 5000;
  int reconnect_delay_ms = 2000;
  int framerate = 0;
  int bitstream_buffer_bytes = 8 * 1024 * 1024;
  bool zero_copy = false;
};

/** Annex B 视频编码种类。 */
enum class VideoCodec { kH264, kH265, kMjpeg };

/** 图像发布使用的话题与消息形式。 */
enum class PublicationMode { kRosImage, kRosJpeg, kHbmem };

/** 验证启动参数；成功时返回空字符串。 */
[[nodiscard]] std::string validate_config(const Config& config);

/** 根据输出参数选择唯一的图像发布方式。 */
[[nodiscard]] PublicationMode publication_mode(const Config& config) noexcept;

/** 设置 RTSP 客户端传输和 socket 超时选项。 */
void configure_rtsp_options(const Config& config, AVDictionary** options);

/** 检查压缩包是否含有可供解码器启动的关键帧。 */
[[nodiscard]] bool contains_keyframe(VideoCodec codec, std::span<const uint8_t> packet) noexcept;

/** 判断 NV12 图像是否可装入 HbmMsg1080P 的数据区。 */
[[nodiscard]] bool fits_hbmem(int width, int height) noexcept;

/** 将带行跨度的 NV12 双平面复制为紧凑布局。 */
[[nodiscard]] bool copy_nv12(const uint8_t* y,
                             const uint8_t* uv,
                             int width,
                             int height,
                             int y_stride,
                             int uv_stride,
                             std::vector<uint8_t>& output);

/** 用 RTCP/NTP 绝对基准和流 PTS 计算 Unix 微秒时间。 */
[[nodiscard]] std::optional<int64_t> camera_stamp(int64_t realtime_base_us,
                                                  int64_t pts,
                                                  AVRational time_base) noexcept;

/** 记录服务请求的采集开关状态。 */
class CaptureState {
 public:
  /** 新节点默认启动采集。 */
  CaptureState() = default;

  /** 返回当前是否要求采集。 */
  [[nodiscard]] bool enabled() const noexcept;

  /** 设置采集开关。 */
  void set_enabled(bool enabled) noexcept;

 private:
  std::atomic_bool enabled_{true};
};

/** 会话结束后计算重连等待时间；暂停或退出时返回空值。 */
[[nodiscard]] std::optional<std::chrono::milliseconds> reconnect_wait(
    const Config& config, bool stopping, const CaptureState& capture_state) noexcept;

/** 等待首个关键帧后才允许向解码器提交数据。 */
class KeyframeGate {
 public:
  /** 返回当前压缩包是否可以送入解码器。 */
  [[nodiscard]] bool accept(bool keyframe) noexcept;

 private:
  bool started_ = false;
};

}  // namespace nl::rtsp_cam
#endif  // NL_RTSP_CAM_FRAME_UTILS_HPP_
