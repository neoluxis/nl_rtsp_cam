// 验证统一相机文件与独立 ROS 标定文件的 CameraInfo 解析。
#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"
#include "nl_rtsp_cam/calibration.hpp"

namespace nl::rtsp_cam {
namespace {

std::string write_fixture(const std::string& text) {
  const auto path = std::filesystem::temp_directory_path() / "nl_rtsp_cam_calibration_test.yaml";
  std::ofstream(path) << text;
  return path.string();
}

constexpr const char* kCalibration = R"(
image_width: 640
image_height: 512
distortion_model: plumb_bob
camera_matrix: {data: [500, 0, 320, 0, 500, 256, 0, 0, 1]}
rectification_matrix: {data: [1, 0, 0, 0, 1, 0, 0, 0, 1]}
projection_matrix: {data: [500, 0, 320, 0, 0, 500, 256, 0, 0, 0, 1, 0]}
distortion_coefficients: {data: [-0.1, 0.01, 0, 0, 0]}
)";

TEST(CalibrationTest, ReadsStandaloneRosYaml) {
  const auto info = load_calibration(write_fixture(kCalibration));
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->width, 640u);
  EXPECT_DOUBLE_EQ(info->k[0], 500.0);
}

TEST(CalibrationTest, ReadsNestedCameraInfo) {
  std::string contents = "cameras: []\nintrinsic: {}\nextrinsic: []\ncamera_info:\n";
  std::string original = kCalibration;
  for (const char character : original) {
    contents.push_back(character);
    if (character == '\n')
      contents.append("  ");
  }
  const auto info = load_calibration(write_fixture(contents));
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->height, 512u);
  EXPECT_EQ(info->d.size(), 5u);
}

TEST(CalibrationTest, MissingNestedCameraInfoDisablesPublication) {
  const auto info = load_calibration(write_fixture("cameras: []\nintrinsic: {}\nextrinsic: []\n"));
  EXPECT_FALSE(info.has_value());
}

}  // namespace
}  // namespace nl::rtsp_cam
