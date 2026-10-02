// S100 多媒体硬件编解码器的 RAII 接口。
#ifndef NL_RTSP_CAM_S100_CODEC_HPP_
#define NL_RTSP_CAM_S100_CODEC_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

extern "C" {
#include "hb_media_codec.h"
}

namespace nl::rtsp_cam {

/** 一帧紧凑排列的 NV12 图像。 */
struct Nv12Frame {
  int width = 0;
  int height = 0;
  int64_t pts = 0;
  std::vector<uint8_t> data;
};

/** S100 硬件视频解码器。 */
class S100Decoder {
 public:
  /** 按流编码和分辨率初始化硬件解码器。 */
  S100Decoder(media_codec_id_t codec, int width, int height, size_t bitstream_buffer_bytes);
  /** 停止并释放硬件缓冲区。 */
  ~S100Decoder();
  S100Decoder(const S100Decoder&) = delete;
  S100Decoder& operator=(const S100Decoder&) = delete;

  /** 提交一个完整的压缩视频帧。 */
  void submit(const uint8_t* data, size_t size, int64_t pts);
  /** 取得已解码的图像；暂时无图像时返回空值。 */
  [[nodiscard]] std::optional<Nv12Frame> take(int timeout_ms);

 private:
  media_codec_context_t context_{};
  bool initialized_ = false;
  bool started_ = false;
  size_t input_capacity_ = 0;
};

/** S100 硬件 JPEG 编码器。 */
class S100JpegEncoder {
 public:
  /** 为指定的 NV12 分辨率初始化 JPEG 编码器。 */
  S100JpegEncoder(int width, int height);
  /** 停止并释放硬件缓冲区。 */
  ~S100JpegEncoder();
  S100JpegEncoder(const S100JpegEncoder&) = delete;
  S100JpegEncoder& operator=(const S100JpegEncoder&) = delete;

  /** 编码一帧紧凑排列的 NV12 图像。 */
  [[nodiscard]] std::vector<uint8_t> encode(const Nv12Frame& frame);

 private:
  media_codec_context_t context_{};
  bool initialized_ = false;
  bool started_ = false;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace nl::rtsp_cam
#endif  // NL_RTSP_CAM_S100_CODEC_HPP_
