// 从相机配置或独立 ROS 标定文件读取 CameraInfo。
#ifndef NL_RTSP_CAM_CALIBRATION_HPP_
#define NL_RTSP_CAM_CALIBRATION_HPP_

#include <optional>
#include <string>

#include "sensor_msgs/msg/camera_info.hpp"

namespace nl::rtsp_cam {

/** 读取可选标定；统一相机文件缺少 camera_info 时返回空值。 */
std::optional<sensor_msgs::msg::CameraInfo> load_calibration(const std::string& path);

}  // namespace nl::rtsp_cam

#endif  // NL_RTSP_CAM_CALIBRATION_HPP_
