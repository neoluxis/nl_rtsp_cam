# S100 RTSP ROS 2 摄像头包的构建入口。
cmake_minimum_required(VERSION 3.22)
project(nl_rtsp_cam VERSION 0.4.0 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(hbm_img_msgs REQUIRED)
find_package(nl_image_msgs REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(yaml-cpp REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
find_path(S100_MEDIA_INCLUDE_DIR hb_media_basic_types.h PATHS /usr/hobot/include REQUIRED)
find_library(HB_MULTIMEDIA_LIBRARY multimedia PATHS /usr/hobot/lib REQUIRED)

add_library(nl_rtsp_cam_frame_utils STATIC src/frame_utils.cpp)
target_compile_features(nl_rtsp_cam_frame_utils PUBLIC cxx_std_20)
target_include_directories(nl_rtsp_cam_frame_utils PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(nl_rtsp_cam_frame_utils PUBLIC PkgConfig::AVUTIL)

add_library(nl_rtsp_cam_core STATIC src/s100_codec.cpp)
target_compile_features(nl_rtsp_cam_core PUBLIC cxx_std_20)
target_include_directories(nl_rtsp_cam_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(nl_rtsp_cam_core PUBLIC ${S100_MEDIA_INCLUDE_DIR})
target_link_libraries(nl_rtsp_cam_core PRIVATE nl_rtsp_cam_frame_utils ${HB_MULTIMEDIA_LIBRARY})

add_executable(nl_rtsp_cam src/nl_rtsp_cam_node.cpp)
target_compile_features(nl_rtsp_cam PRIVATE cxx_std_20)
target_include_directories(nl_rtsp_cam PRIVATE ${S100_MEDIA_INCLUDE_DIR})
target_link_libraries(nl_rtsp_cam nl_rtsp_cam_core nl_rtsp_cam_frame_utils PkgConfig::AVFORMAT
  PkgConfig::AVCODEC PkgConfig::AVUTIL yaml-cpp ${HB_MULTIMEDIA_LIBRARY})
set_target_properties(nl_rtsp_cam PROPERTIES BUILD_RPATH "/usr/hobot/lib"
  INSTALL_RPATH "/usr/hobot/lib")
ament_target_dependencies(nl_rtsp_cam rclcpp std_msgs sensor_msgs std_srvs hbm_img_msgs nl_image_msgs)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(nl_rtsp_cam_frame_utils PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(nl_rtsp_cam_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(nl_rtsp_cam PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS nl_rtsp_cam DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

option(BUILD_NLX_TESTS "Build all NLX tests" OFF)
option(BUILD_NL_RTSP_CAM_TESTS "Build RTSP camera tests" OFF)
if(BUILD_NLX_TESTS AND BUILD_NL_RTSP_CAM_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

ament_package()
