// 调用 S100 libmm 硬解和 JPEG 硬编，并按作用域归还输出缓冲区。
#include "nl_rtsp_cam/s100_codec.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "hb_media_error.h"
#include "nl_rtsp_cam/frame_utils.hpp"

namespace nl::rtsp_cam {
namespace {

void check_result(int result, const char* action) {
  if (result != 0)
    throw std::runtime_error(std::string(action) + " failed: " + std::to_string(result));
}

size_t aligned_bitstream_size(int width, int height) {
  const size_t size = static_cast<size_t>(width) * height * 3 / 2;
  return (size + 4095) & ~static_cast<size_t>(4095);
}

}  // namespace

S100Decoder::S100Decoder(media_codec_id_t codec,
                         int width,
                         int height,
                         size_t bitstream_buffer_bytes) {
  if (!fits_hbmem(width, height))
    throw std::invalid_argument("unsupported resolution");
  context_.codec_id = codec;
  context_.encoder = false;
  auto& params = context_.video_dec_params;
  params.feed_mode = MC_FEEDING_MODE_FRAME_SIZE;
  params.pix_fmt = MC_PIXEL_FORMAT_NV12;
  params.frame_buf_count = 4;
  params.bitstream_buf_count = 4;
  input_capacity_ = std::max(aligned_bitstream_size(width, height),
                             (bitstream_buffer_bytes + 4095) & ~static_cast<size_t>(4095));
  params.bitstream_buf_size = input_capacity_;
  if (codec == MEDIA_CODEC_ID_H264) {
    params.h264_dec_config.reorder_enable = true;
  } else if (codec == MEDIA_CODEC_ID_H265) {
    params.h265_dec_config.reorder_enable = true;
  } else if (codec == MEDIA_CODEC_ID_MJPEG) {
    context_.codec_id = MEDIA_CODEC_ID_JPEG;
    params.mjpeg_dec_config.frame_crop_enable = false;
    params.jpeg_dec_config.frame_crop_enable = false;
  } else {
    throw std::invalid_argument("unsupported video codec");
  }
  try {
    check_result(hb_mm_mc_initialize(&context_), "decoder initialize");
    initialized_ = true;
    check_result(hb_mm_mc_configure(&context_), "decoder configure");
    mc_av_codec_startup_params_t startup{};
    check_result(hb_mm_mc_start(&context_, &startup), "decoder start");
    started_ = true;
  } catch (...) {
    if (initialized_)
      hb_mm_mc_release(&context_);
    throw;
  }
}

S100Decoder::~S100Decoder() {
  if (started_)
    hb_mm_mc_stop(&context_);
  if (initialized_)
    hb_mm_mc_release(&context_);
}

void S100Decoder::submit(const uint8_t* data, size_t size, int64_t pts) {
  if (data == nullptr || size == 0 || size > input_capacity_)
    throw std::invalid_argument("decoder packet exceeds configured bitstream buffer");
  media_codec_buffer_t input{};
  check_result(hb_mm_mc_dequeue_input_buffer(&context_, &input, 100), "decoder input dequeue");
  if (input.vstream_buf.vir_ptr == nullptr || size > input.vstream_buf.size)
    throw std::runtime_error("decoder input buffer too small");
  std::memcpy(input.vstream_buf.vir_ptr, data, size);
  input.vstream_buf.size = size;
  input.vstream_buf.pts = pts < 0 ? 0 : static_cast<uint64_t>(pts);
  check_result(hb_mm_mc_queue_input_buffer(&context_, &input, 100), "decoder input queue");
}

std::optional<Nv12Frame> S100Decoder::take(int timeout_ms) {
  media_codec_buffer_t output{};
  const int result = hb_mm_mc_dequeue_output_buffer(&context_, &output, nullptr, timeout_ms);
  if (result == HB_MEDIA_ERR_WAIT_TIMEOUT)
    return std::nullopt;
  check_result(result, "decoder output dequeue");
  // 解码器输出缓冲区必须在本轮复制完成后归还。
  Nv12Frame frame;
  try {
    frame.width = output.vframe_buf.width;
    frame.height = output.vframe_buf.height;
    frame.pts = static_cast<int64_t>(output.vframe_buf.pts);
    const int y_stride = output.vframe_buf.stride > 0 ? output.vframe_buf.stride : frame.width;
    const int uv_stride = output.vframe_buf.vstride > 0 ? output.vframe_buf.vstride : y_stride;
    if (!copy_nv12(output.vframe_buf.vir_ptr[0],
                   output.vframe_buf.vir_ptr[1],
                   frame.width,
                   frame.height,
                   y_stride,
                   uv_stride,
                   frame.data))
      throw std::runtime_error("invalid NV12 decoder output");
  } catch (...) {
    hb_mm_mc_queue_output_buffer(&context_, &output, 100);
    throw;
  }
  check_result(hb_mm_mc_queue_output_buffer(&context_, &output, 100), "decoder output queue");
  return frame;
}

S100JpegEncoder::S100JpegEncoder(int width, int height) : width_(width), height_(height) {
  if (!fits_hbmem(width, height))
    throw std::invalid_argument("unsupported resolution");
  context_.codec_id = MEDIA_CODEC_ID_JPEG;
  context_.encoder = true;
  auto& params = context_.video_enc_params;
  params.width = width;
  params.height = height;
  params.pix_fmt = MC_PIXEL_FORMAT_NV12;
  params.bitstream_buf_size = aligned_bitstream_size(width, height);
  params.frame_buf_count = 4;
  params.bitstream_buf_count = 4;
  params.jpeg_enc_config.quality_factor = 85;
  try {
    check_result(hb_mm_mc_initialize(&context_), "encoder initialize");
    initialized_ = true;
    check_result(hb_mm_mc_configure(&context_), "encoder configure");
    mc_av_codec_startup_params_t startup{};
    check_result(hb_mm_mc_start(&context_, &startup), "encoder start");
    started_ = true;
  } catch (...) {
    if (initialized_)
      hb_mm_mc_release(&context_);
    throw;
  }
}

S100JpegEncoder::~S100JpegEncoder() {
  if (started_)
    hb_mm_mc_stop(&context_);
  if (initialized_)
    hb_mm_mc_release(&context_);
}

std::vector<uint8_t> S100JpegEncoder::encode(const Nv12Frame& frame) {
  if (frame.width != width_ || frame.height != height_ ||
      frame.data.size() != static_cast<size_t>(width_) * height_ * 3 / 2)
    throw std::invalid_argument("invalid encoder frame");
  media_codec_buffer_t input{};
  check_result(hb_mm_mc_dequeue_input_buffer(&context_, &input, 100), "encoder input dequeue");
  const int y_stride = input.vframe_buf.stride > 0 ? input.vframe_buf.stride : width_;
  const int uv_stride = input.vframe_buf.vstride > 0 ? input.vframe_buf.vstride : y_stride;
  if (input.vframe_buf.vir_ptr[0] == nullptr || input.vframe_buf.vir_ptr[1] == nullptr ||
      y_stride < width_ || uv_stride < width_)
    throw std::runtime_error("invalid encoder input");
  const size_t plane_size = static_cast<size_t>(width_) * height_;
  for (int row = 0; row < height_; ++row)
    std::memcpy(input.vframe_buf.vir_ptr[0] + static_cast<size_t>(row) * y_stride,
                frame.data.data() + static_cast<size_t>(row) * width_,
                width_);
  for (int row = 0; row < height_ / 2; ++row)
    std::memcpy(input.vframe_buf.vir_ptr[1] + static_cast<size_t>(row) * uv_stride,
                frame.data.data() + plane_size + static_cast<size_t>(row) * width_,
                width_);
  input.vframe_buf.pts = frame.pts < 0 ? 0 : static_cast<uint64_t>(frame.pts);
  check_result(hb_mm_mc_queue_input_buffer(&context_, &input, 100), "encoder input queue");
  media_codec_buffer_t output{};
  check_result(hb_mm_mc_dequeue_output_buffer(&context_, &output, nullptr, 1000),
               "encoder output dequeue");
  std::vector<uint8_t> jpeg;
  try {
    if (output.vstream_buf.vir_ptr == nullptr || output.vstream_buf.size == 0)
      throw std::runtime_error("empty JPEG output");
    jpeg.assign(output.vstream_buf.vir_ptr, output.vstream_buf.vir_ptr + output.vstream_buf.size);
  } catch (...) {
    hb_mm_mc_queue_output_buffer(&context_, &output, 100);
    throw;
  }
  check_result(hb_mm_mc_queue_output_buffer(&context_, &output, 100), "encoder output queue");
  return jpeg;
}

}  // namespace nl::rtsp_cam
