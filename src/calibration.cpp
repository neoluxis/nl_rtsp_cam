// 解析统一相机配置中的原始 ROS 标定，同时支持采集节点直接使用独立标定文件。
#include "nl_rtsp_cam/calibration.hpp"

#include <cstdint>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace nl::rtsp_cam {

std::optional<sensor_msgs::msg::CameraInfo> load_calibration(const std::string& path) {
  if (path.empty())
    return std::nullopt;
  const YAML::Node root = YAML::LoadFile(path);
  // bringup 传入统一相机文件；独立使用本节点时仍可传入传统 ROS 标定文件。
  const YAML::Node doc = root["camera_info"] ? root["camera_info"] : root;
  if (root["cameras"] && !root["camera_info"])
    return std::nullopt;
  sensor_msgs::msg::CameraInfo info;
  info.width = doc["image_width"].as<uint32_t>();
  info.height = doc["image_height"].as<uint32_t>();
  info.distortion_model = doc["distortion_model"].as<std::string>();
  const auto k = doc["camera_matrix"]["data"];
  const auto r = doc["rectification_matrix"]["data"];
  const auto p = doc["projection_matrix"]["data"];
  const auto d = doc["distortion_coefficients"]["data"];
  if (k.size() != 9 || r.size() != 9 || p.size() != 12 || d.size() == 0)
    throw std::runtime_error("invalid camera calibration matrix");
  for (size_t i = 0; i < 9; ++i) {
    info.k[i] = k[i].as<double>();
    info.r[i] = r[i].as<double>();
  }
  for (size_t i = 0; i < 12; ++i)
    info.p[i] = p[i].as<double>();
  for (const auto& value : d)
    info.d.push_back(value.as<double>());
  return info;
}

}  // namespace nl::rtsp_cam
