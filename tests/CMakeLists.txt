# 此目录只注册按需启用的行为测试。
option(BUILD_NL_RTSP_CAM_FRAME_TESTS "Build RTSP frame behavior tests" OFF)
if(BUILD_NLX_TESTS AND BUILD_NL_RTSP_CAM_TESTS AND BUILD_NL_RTSP_CAM_FRAME_TESTS)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(nl_rtsp_cam_frame_tests frame_utils_test.cpp)
  target_link_libraries(nl_rtsp_cam_frame_tests nl_rtsp_cam_frame_utils)
endif()
