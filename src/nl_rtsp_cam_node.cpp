// 单路 RTSP ROS 2 节点：拆流、S100 硬件编解码、发布和断线重连。
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

#include "hbm_img_msgs/msg/hbm_msg1080_p.hpp"
#include "nl_image_msgs/msg/frame_metadata.hpp"
#include "nl_rtsp_cam/calibration.hpp"
#include "nl_rtsp_cam/frame_utils.hpp"
#include "nl_rtsp_cam/s100_codec.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/compressed_image.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/header.hpp"
#include "std_srvs/srv/set_bool.hpp"

namespace nl::rtsp_cam {
namespace {

int64_t steady_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

media_codec_id_t hardware_codec(AVCodecID codec) {
  switch (codec) {
    case AV_CODEC_ID_H264:
      return MEDIA_CODEC_ID_H264;
    case AV_CODEC_ID_HEVC:
      return MEDIA_CODEC_ID_H265;
    case AV_CODEC_ID_MJPEG:
      return MEDIA_CODEC_ID_MJPEG;
    default:
      throw std::runtime_error("unsupported RTSP video codec");
  }
}

struct FormatCloser {
  void operator()(AVFormatContext* context) const {
    if (context != nullptr)
      avformat_close_input(&context);
  }
};
struct PacketCloser {
  void operator()(AVPacket* packet) const {
    av_packet_free(&packet);
  }
};
struct FilterCloser {
  void operator()(AVBSFContext* filter) const {
    av_bsf_free(&filter);
  }
};

}  // namespace

/** 将一条 RTSP 视频流发布为 ROS 图像的节点。 */
class RtspCamNode final : public rclcpp::Node {
 public:
  /** 读取参数并启动后台采集线程。 */
  explicit RtspCamNode(const rclcpp::NodeOptions& options) : Node("nl_rtsp_cam", options) {
    config_.rtsp_url = declare_parameter<std::string>("rtsp_url", "");
    config_.transport = declare_parameter<std::string>("rtsp_transport", "tcp");
    config_.pixel_format = declare_parameter<std::string>("pixel_format", "nv12");
    config_.timestamp_source = declare_parameter<std::string>("timestamp_source", "receive");
    config_.frame_id = declare_parameter<std::string>("frame_id", "rtsp_cam");
    config_.node_id = declare_parameter<std::string>("node_id", "local");
    config_.camera_id = declare_parameter<std::string>("camera_id", "camera0");
    config_.camera_calibration_file_path =
        declare_parameter<std::string>("camera_calibration_file_path", "");
    config_.connect_timeout_ms = declare_parameter<int>("connect_timeout_ms", 5000);
    config_.read_timeout_ms = declare_parameter<int>("read_timeout_ms", 5000);
    config_.reconnect_delay_ms = declare_parameter<int>("reconnect_delay_ms", 2000);
    config_.framerate = declare_parameter<int>("framerate", 0);
    config_.ros_image_fps = declare_parameter<int>("ros_image_fps", 0);
    config_.bitstream_buffer_bytes =
        declare_parameter<int>("bitstream_buffer_bytes", 8 * 1024 * 1024);
    config_.zero_copy = declare_parameter<bool>("zero_copy", false);
    config_.publish_ros_image = declare_parameter<bool>("publish_ros_image", false);
    const auto error = validate_config(config_);
    if (!error.empty())
      throw std::invalid_argument(error);
    const auto mode = publication_mode(config_);
    if (mode == PublicationMode::kHbmem) {
      hbmem_publisher_ =
          create_publisher<hbm_img_msgs::msg::HbmMsg1080P>("hbmem_img", rclcpp::SensorDataQoS());
      if (config_.publish_ros_image) {
        // 共享内存图像供板端 DNN 使用；标准 ROS 图像仅在外部订阅时复制发布。
        image_publisher_ = create_publisher<sensor_msgs::msg::Image>(
            "image", rclcpp::SensorDataQoS().keep_last(1));
        ros_image_rate_limiter_ = std::make_unique<FrameRateLimiter>(config_.ros_image_fps);
      }
    } else if (mode == PublicationMode::kRosJpeg) {
      jpeg_publisher_ =
          create_publisher<sensor_msgs::msg::CompressedImage>("image", rclcpp::QoS(10));
    } else {
      image_publisher_ = create_publisher<sensor_msgs::msg::Image>("image", rclcpp::QoS(10));
    }
    camera_info_publisher_ =
        create_publisher<sensor_msgs::msg::CameraInfo>("camera_info", rclcpp::QoS(10));
    frame_metadata_publisher_ =
        create_publisher<nl_image_msgs::msg::FrameMetadata>("frame_metadata", rclcpp::SensorDataQoS());
    try {
      calibration_ = load_calibration(config_.camera_calibration_file_path);
    } catch (const std::exception& e) {
      RCLCPP_WARN(get_logger(), "Camera calibration disabled: %s", e.what());
    }
    capture_service_ = create_service<std_srvs::srv::SetBool>(
        "set_capture",
        [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
               std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
          capture_state_.set_enabled(request->data);
          state_cv_.notify_all();
          response->success = true;
          response->message = request->data ? "capture started" : "capture stopped";
        });
    worker_ = std::thread([this] { run(); });
  }

  /** 中断 FFmpeg 读取并等待后台线程退出。 */
  ~RtspCamNode() override {
    stopping_.store(true);
    state_cv_.notify_all();
    if (worker_.joinable())
      worker_.join();
  }

 private:
  static int interrupt_callback(void* opaque) {
    auto* node = static_cast<RtspCamNode*>(opaque);
    return node->stopping_.load() || !node->capture_state_.enabled() ||
           steady_millis() >= node->io_deadline_ms_.load();
  }

  void reset_deadline(int timeout_ms) {
    io_deadline_ms_.store(steady_millis() + timeout_ms);
  }

  void run() {
    while (!stopping_.load()) {
      if (!capture_state_.enabled()) {
        std::unique_lock<std::mutex> lock(state_mutex_);
        state_cv_.wait(lock, [this] { return stopping_.load() || capture_state_.enabled(); });
        continue;
      }
      try {
        read_session();
      } catch (const std::exception& e) {
        if (capture_state_.enabled() && !stopping_.load())
          RCLCPP_WARN(get_logger(), "RTSP session ended: %s", e.what());
      }
      const auto retry_delay = reconnect_wait(config_, stopping_.load(), capture_state_);
      if (!retry_delay)
        continue;
      std::unique_lock<std::mutex> lock(state_mutex_);
      state_cv_.wait_for(
          lock, *retry_delay, [this] { return stopping_.load() || !capture_state_.enabled(); });
    }
  }

  void read_session() {
    std::unique_ptr<AVFormatContext, FormatCloser> format(avformat_alloc_context());
    if (!format)
      throw std::runtime_error("FFmpeg context allocation failed");
    format->interrupt_callback = {&RtspCamNode::interrupt_callback, this};
    AVDictionary* options = nullptr;
    configure_rtsp_options(config_, &options);
    AVFormatContext* raw = format.release();
    reset_deadline(config_.connect_timeout_ms);
    const int open_result = avformat_open_input(&raw, config_.rtsp_url.c_str(), nullptr, &options);
    av_dict_free(&options);
    format.reset(raw);
    if (open_result < 0)
      throw std::runtime_error("RTSP open failed");
    reset_deadline(config_.connect_timeout_ms);
    if (avformat_find_stream_info(format.get(), nullptr) < 0)
      throw std::runtime_error("RTSP stream info unavailable");
    const int stream_index =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0)
      throw std::runtime_error("no video stream");
    AVStream* stream = format->streams[stream_index];
    const auto codec = hardware_codec(stream->codecpar->codec_id);
    const auto video_codec = codec == MEDIA_CODEC_ID_H264   ? VideoCodec::kH264
                             : codec == MEDIA_CODEC_ID_H265 ? VideoCodec::kH265
                                                            : VideoCodec::kMjpeg;
    if (stream->codecpar->width > 1920 || stream->codecpar->height > 1080 ||
        !fits_hbmem(stream->codecpar->width, stream->codecpar->height))
      throw std::runtime_error("unsupported video resolution");
    S100Decoder decoder(
        codec, stream->codecpar->width, stream->codecpar->height, config_.bitstream_buffer_bytes);
    std::unique_ptr<S100JpegEncoder> encoder;
    if (config_.pixel_format == "jpeg")
      encoder =
          std::make_unique<S100JpegEncoder>(stream->codecpar->width, stream->codecpar->height);

    std::unique_ptr<AVBSFContext, FilterCloser> filter;
    if (codec == MEDIA_CODEC_ID_H264 || codec == MEDIA_CODEC_ID_H265) {
      const char* name = codec == MEDIA_CODEC_ID_H264 ? "h264_mp4toannexb" : "hevc_mp4toannexb";
      const AVBitStreamFilter* description = av_bsf_get_by_name(name);
      AVBSFContext* raw_filter = nullptr;
      if (description == nullptr || av_bsf_alloc(description, &raw_filter) < 0)
        throw std::runtime_error("bitstream filter unavailable");
      filter.reset(raw_filter);
      if (avcodec_parameters_copy(filter->par_in, stream->codecpar) < 0)
        throw std::runtime_error("bitstream filter parameters failed");
      filter->time_base_in = stream->time_base;
      if (av_bsf_init(filter.get()) < 0)
        throw std::runtime_error("bitstream filter init failed");
    }
    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    std::unique_ptr<AVPacket, PacketCloser> filtered(av_packet_alloc());
    if (!packet || !filtered)
      throw std::runtime_error("packet allocation failed");
    KeyframeGate keyframe_gate;
    FrameRateLimiter rate_limiter(config_.framerate);
    while (!stopping_.load() && capture_state_.enabled()) {
      reset_deadline(config_.read_timeout_ms);
      const int result = av_read_frame(format.get(), packet.get());
      if (result < 0)
        throw std::runtime_error("RTSP read failed or timed out");
      if (packet->stream_index != stream_index) {
        av_packet_unref(packet.get());
        continue;
      }
      if (config_.timestamp_source == "camera" && packet->pts < 0) {
        RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 5000, "Waiting for valid camera packet PTS");
        av_packet_unref(packet.get());
        continue;
      }
      const bool is_keyframe =
          (packet->flags & AV_PKT_FLAG_KEY) != 0 ||
          contains_keyframe(video_codec,
                            std::span(packet->data, static_cast<size_t>(packet->size)));
      if (!keyframe_gate.accept(is_keyframe)) {
        av_packet_unref(packet.get());
        continue;
      }
      if (filter) {
        if (av_bsf_send_packet(filter.get(), packet.get()) < 0)
          throw std::runtime_error("bitstream filter input failed");
        int filter_result = 0;
        while ((filter_result = av_bsf_receive_packet(filter.get(), filtered.get())) == 0) {
          process_packet(*filtered, decoder, encoder.get(), *format, *stream, rate_limiter);
          av_packet_unref(filtered.get());
        }
        if (filter_result != AVERROR(EAGAIN) && filter_result != AVERROR_EOF)
          throw std::runtime_error("bitstream filter output failed");
      } else {
        process_packet(*packet, decoder, encoder.get(), *format, *stream, rate_limiter);
      }
      av_packet_unref(packet.get());
    }
  }

  void process_packet(const AVPacket& packet,
                      S100Decoder& decoder,
                      S100JpegEncoder* encoder,
                      const AVFormatContext& format,
                      const AVStream& stream,
                      FrameRateLimiter& rate_limiter) {
    if (packet.size <= 0)
      return;
    decoder.submit(packet.data, packet.size, packet.pts);
    for (;;) {
      auto frame = decoder.take(10);
      if (!frame)
        break;
      const auto now = std::chrono::steady_clock::now();
      if (!rate_limiter.allow(now))
        continue;
      std_msgs::msg::Header header;
      header.frame_id = config_.frame_id;
      if (config_.timestamp_source == "camera") {
        const auto stamp = camera_stamp(format.start_time_realtime, frame->pts, stream.time_base);
        if (!stamp) {
          RCLCPP_WARN_THROTTLE(
              get_logger(), *get_clock(), 5000, "Waiting for RTCP/NTP camera timestamp");
          continue;
        }
        header.stamp = rclcpp::Time(*stamp * 1000);
      } else {
        header.stamp = this->now();
      }
      publish_frame(*frame, encoder, header);
    }
  }

  void publish_frame(const Nv12Frame& frame,
                     S100JpegEncoder* encoder,
                     const std_msgs::msg::Header& header) {
    const uint32_t frame_index = frame_index_++;
    std::vector<uint8_t> jpeg;
    const std::vector<uint8_t>* data = &frame.data;
    if (encoder != nullptr) {
      jpeg = encoder->encode(frame);
      data = &jpeg;
    }
    if (config_.zero_copy) {
      auto loan = hbmem_publisher_->borrow_loaned_message();
      if (!loan.is_valid())
        throw std::runtime_error("hbmem loan unavailable");
      auto& message = loan.get();
      if (data->size() > message.data.size())
        throw std::runtime_error("hbmem data capacity exceeded");
      message.index = frame_index;
      message.time_stamp = header.stamp;
      message.width = frame.width;
      message.height = frame.height;
      message.step = encoder == nullptr ? frame.width : 0;
      message.data_size = data->size();
      std::fill(message.encoding.begin(), message.encoding.end(), 0);
      const char* encoding = encoder == nullptr ? "nv12" : "jpeg";
      std::memcpy(message.encoding.data(), encoding, std::strlen(encoding));
      std::copy(data->begin(), data->end(), message.data.begin());
      hbmem_publisher_->publish(std::move(loan));
      if (image_publisher_ && image_publisher_->get_subscription_count() > 0 &&
          ros_image_rate_limiter_->allow(std::chrono::steady_clock::now())) {
        sensor_msgs::msg::Image message;
        message.header = header;
        message.width = frame.width;
        message.height = frame.height;
        message.encoding = "nv12";
        message.step = frame.width;
        message.is_bigendian = false;
        message.data = frame.data;
        image_publisher_->publish(message);
      }
    } else if (encoder != nullptr) {
      sensor_msgs::msg::CompressedImage message;
      message.header = header;
      message.format = "jpeg";
      message.data = std::move(jpeg);
      jpeg_publisher_->publish(message);
    } else {
      sensor_msgs::msg::Image message;
      message.header = header;
      message.width = frame.width;
      message.height = frame.height;
      message.encoding = "nv12";
      message.step = frame.width;
      message.is_bigendian = false;
      message.data = frame.data;
      image_publisher_->publish(message);
    }
    nl_image_msgs::msg::FrameMetadata metadata;
    metadata.header = header;
    metadata.node_id = config_.node_id;
    metadata.camera_id = config_.camera_id;
    metadata.frame_index = frame_index;
    metadata.width = frame.width;
    metadata.height = frame.height;
    frame_metadata_publisher_->publish(metadata);
    if (calibration_) {
      if (calibration_->width != static_cast<uint32_t>(frame.width) ||
          calibration_->height != static_cast<uint32_t>(frame.height)) {
        RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 5000, "Calibration resolution does not match video");
      } else {
        auto info = *calibration_;
        info.header = header;
        camera_info_publisher_->publish(info);
      }
    }
  }

  Config config_;
  CaptureState capture_state_;
  std::atomic_bool stopping_{false};
  std::atomic<int64_t> io_deadline_ms_{0};
  std::mutex state_mutex_;
  std::condition_variable state_cv_;
  std::thread worker_;
  uint32_t frame_index_ = 0;
  std::optional<sensor_msgs::msg::CameraInfo> calibration_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_publisher_;
  std::unique_ptr<FrameRateLimiter> ros_image_rate_limiter_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr jpeg_publisher_;
  rclcpp::Publisher<hbm_img_msgs::msg::HbmMsg1080P>::SharedPtr hbmem_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr camera_info_publisher_;
  rclcpp::Publisher<nl_image_msgs::msg::FrameMetadata>::SharedPtr frame_metadata_publisher_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr capture_service_;
};

}  // namespace nl::rtsp_cam

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<nl::rtsp_cam::RtspCamNode>(rclcpp::NodeOptions{}));
  } catch (const std::exception& e) {
    RCLCPP_FATAL(rclcpp::get_logger("nl_rtsp_cam"), "%s", e.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
