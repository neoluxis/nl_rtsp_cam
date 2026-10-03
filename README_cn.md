---
author: N. C. Lee
created on: 2026-10-01
updated on: 2026-10-03
version: 0.4.0
title: S100 RTSP 摄像头 ROS 2 包
tags:
  - ROS 2
  - RTSP
---

# S100 RTSP 摄像头 ROS 2 包

## 更新记录

| 日期 | 版本 | 作者 | 说明 |
| --- | --- | --- | --- |
| 2026-10-03 | 0.4.0 | N. C. Lee | 共享内存取流时可按需同步发布限帧标准 ROS 图像。 |
| 2026-10-03 | 0.3.0 | N. C. Lee | 调整限帧器，减少多路相机到帧抖动导致的误丢帧。 |
| 2026-10-03 | 0.2.0 | N. C. Lee | 增加每帧节点、相机和帧序号元数据。 |
| 2026-10-01 | 0.1.0 | N. C. Lee | 新增单路 RTSP、S100 硬解、JPEG 输出和 launch。 |

## 功能与环境

`nl_rtsp_cam` 每个节点连接一路 RTSP 视频，以 FFmpeg 拆流，经 RDK S100 `libmm` 硬解 H.264、H.265 或 MJPEG 后发布 NV12 图像。可选择硬件编码 JPEG，也可用 `HbmMsg1080P` 发布。目标系统为 RDK S100 的 TROS Humble；最高输入分辨率为 1920×1080，图像宽高必须为偶数。节点使用 ROS 日志，日志文件位于 ROS 日志目录。

`framerate` 限制平均发布帧率；限帧器允许两帧的短时余量，避免输入与配置同为 24 FPS 时因到帧抖动持续误丢帧。

Orb Ubuntu 用于编译和不依赖硬件的测试；运行硬解与硬编需要 S100。构建前应安装 `hobot-multimedia-dev`、FFmpeg 开发包和 TROS Humble。

## 构建

```bash
source /opt/tros/humble/setup.bash
colcon build --base-paths /Users/neolux/Projects/dk2026003/ai_service/nl_rtsp_cam \
  --packages-select nl_rtsp_cam
source install/setup.bash
```

若系统没有将 `/usr/hobot/lib` 加入动态库搜索路径，直接使用 `ros2 run` 前需设置 `LD_LIBRARY_PATH=/usr/hobot/lib:$LD_LIBRARY_PATH`；launch 文件已设置该路径。

## 启动

```bash
ros2 launch nl_rtsp_cam nl_rtsp_cam.launch.py \
  rtsp_url:='rtsp://user:password@camera/stream' namespace:=camera_01
```

Web 预览使用 JPEG 输出：

```bash
ros2 launch nl_rtsp_cam nl_rtsp_cam_websocket.launch.py \
  rtsp_url:='rtsp://user:password@camera/stream'
```

不要把带密码的 URL 写入仓库文件。每个额外摄像头启动一个独立实例，并使用不同 namespace。停止和重新连接采集：

```bash
ros2 service call /camera_01/set_capture std_srvs/srv/SetBool '{data: false}'
ros2 service call /camera_01/set_capture std_srvs/srv/SetBool '{data: true}'
```

## 接口

| 相对话题或服务 | 类型 | 条件 |
| --- | --- | --- |
| `image` | `sensor_msgs/msg/Image`，编码 `nv12` | 默认 |
| `image` | `sensor_msgs/msg/CompressedImage`，格式 `jpeg` | `pixel_format=jpeg` |
| `hbmem_img` | `hbm_img_msgs/msg/HbmMsg1080P` | `zero_copy=true`，供板端 DNN 使用 |
| `image` | `sensor_msgs/msg/Image`，编码 `nv12` | `zero_copy=true` 且 `publish_ros_image=true`；有订阅者时按 `ros_image_fps` 发布 |
| `camera_info` | `sensor_msgs/msg/CameraInfo` | 标定文件有效且分辨率匹配 |
| `frame_metadata` | `nl_image_msgs/msg/FrameMetadata` | 成功发布图像后提供相同时间戳和帧序号 |
| `set_capture` | `std_srvs/srv/SetBool` | 始终提供；`false` 停止，`true` 重连 |

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `rtsp_url` | 必填 | 单路 RTSP/RTSPS URL |
| `rtsp_transport` | `tcp` | `tcp` 或 `udp` |
| `pixel_format` | `nv12` | `nv12` 或 `jpeg` |
| `zero_copy` | `false` | 使用 `hbmem_img` 话题 |
| `publish_ros_image` | `false` | 共享内存模式下额外提供标准 ROS 原始图像；没有订阅者时不复制 |
| `ros_image_fps` | `0` | 额外 ROS 图像最大发布帧率；`0` 不限速 |
| `timestamp_source` | `receive` | `receive` 为本机时间；`camera` 为 RTCP/NTP 时间 |
| `frame_id` | `rtsp_cam` | 图像与标定消息的帧 ID |
| `node_id` | `local` | 元数据中的源节点 ID |
| `camera_id` | `camera0` | 元数据中的源相机 ID |
| `camera_calibration_file_path` | 空 | ROS 相机标定 YAML 文件 |
| `framerate` | `0` | 最大发布帧率；`0` 不限速 |
| `bitstream_buffer_bytes` | `8388608` | 单个压缩帧的硬解输入缓冲区字节数；可设为 1–64 MiB |
| `connect_timeout_ms` | `5000` | 连接和流信息探测超时 |
| `read_timeout_ms` | `5000` | 视频包读取超时 |
| `reconnect_delay_ms` | `2000` | 断流后的重连间隔 |

`camera` 时间模式要求摄像机已经同步 NTP 并通过 RTCP Sender Report 提供时间基准。基准未到达时节点等待并记录警告，不发布使用错误时间戳的图像。标定文件应使用 ROS 相机标定 YAML 字段 `image_width`、`image_height`、`distortion_model`、`camera_matrix`、`rectification_matrix`、`projection_matrix` 和 `distortion_coefficients`。

测试命令、结果及板端验收方法见测试说明[^testing]。

[^testing]: [nl_rtsp_cam 测试与板端验收](docs/testing.md)
