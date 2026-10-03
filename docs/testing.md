---
author: N. C. Lee
created on: 2026-10-01
updated on: 2026-10-03
version: 0.2.0
title: nl_rtsp_cam 测试与板端验收
tags:
  - Testing
  - ROS 2
---

# nl_rtsp_cam 测试与板端验收

## 更新记录

| 日期 | 版本 | 作者 | 说明 |
| --- | --- | --- | --- |
| 2026-10-03 | 0.2.0 | N. C. Lee | 增加源帧元数据验证要求。 |
| 2026-10-01 | 0.1.0 | N. C. Lee | 增加 Orb 单元测试命令和 S100 验收步骤。 |

## Orb Ubuntu

在 Orb 的 `ubuntu` 虚拟机中运行；源代码由 Orb 共享挂载。测试开关默认全为 `OFF`，须逐层打开：

```bash
source /opt/tros/humble/setup.bash
colcon build --base-paths /Users/neolux/Projects/dk2026003/ai_service/nl_rtsp_cam \
  --packages-select nl_rtsp_cam \
  --cmake-args -DBUILD_NLX_TESTS=ON -DBUILD_NL_RTSP_CAM_TESTS=ON \
  -DBUILD_NL_RTSP_CAM_FRAME_TESTS=ON
ctest --test-dir build/nl_rtsp_cam --output-on-failure
```

GTest 覆盖必填 URL 与参数检查、TCP 客户端超时选项、带行跨度的 NV12 拷贝、hbmem 容量、发布模式、缺失 RTCP/NTP 基准、暂停恢复、重连决策和关键帧门控。Orb 没有 S100 视频设备，因此不能把编译成功视为硬件编解码通过。Orb 系统还有一个与本包无关的包管理故障：`hobot-dnn` 安装脚本因 `patchelf` 版本过低失败；`hobot-multimedia-dev` 的头文件和链接库已就位。

2026-10-01 实际执行：`colcon build` 输出 `1 package finished`；`ctest --test-dir /tmp/nl_rtsp_build/nl_rtsp_cam --output-on-failure` 输出 `100% tests passed, 0 tests failed out of 1`，其 GTest 包含 10 个行为场景。节点对空 URL 输出 `rtsp_url is required` 并以状态码 1 退出。使用不可达的 `rtsp://127.0.0.1:1/stream` 验证每约 2 秒重试；调用 `/set_capture` 的 false 返回 `capture stopped` 并停止重试，true 返回 `capture started` 后恢复重试；SIGINT 后进程正常退出。此检查只验证网络失败与服务控制，不验证图像硬件链路。

两个已安装的 launch 均通过 `ros2 launch nl_rtsp_cam <文件名> --show-args` 参数解析，空 URL 参数检查通过。Web 预览的画面输出仍需板端 RTSP 流验证。

## Sanitizer

在 Orb Ubuntu 中先执行 `source /opt/tros/humble/setup.bash`，再运行以下命令；两组预设都会打开包内 GTest：

```bash
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan -j2
ctest --preset asan-ubsan
cmake --preset tsan
cmake --build --preset tsan -j2
ctest --preset tsan
```

2026-10-01 实际执行：ASan/UBSan 与 TSan 的 CTest 均输出 `100% tests passed, 0 tests failed out of 1`。这些测试只执行不依赖硬件的逻辑；S100 编解码与 ROS 工作线程需按下节实机验证。

## S100 实机验收

使用具备 NTP/RTCP Sender Report 的摄像机，分别配置 H.264、H.265 和 MJPEG RTSP 子码流，分辨率不超过 1920×1080。对每种编码执行：

1. 启动默认 launch，检查 `ros2 topic type /image` 为 `sensor_msgs/msg/Image`，编码为 `nv12`，宽高和数据长度正确；检查稳定发布至少 30 秒。
2. 启动 `pixel_format:=jpeg`，确认 `sensor_msgs/msg/CompressedImage` 的 JPEG 图像可解码；运行 Web 预览 launch 检查画面。
3. 启动 `zero_copy:=true`，确认 `hbmem_img` 的编码、数据大小和时间戳正确，且未发布普通 `image`。
   同时检查 `frame_metadata` 的时间戳、宽高与 `hbmem_img` 一致，帧序号等于 `hbmem_img.index`。
4. 设置标定 YAML，确认 `camera_info` 与图像时间戳和帧 ID 一致；标定分辨率不匹配时不发布标定消息。
5. 依次调用 `set_capture` 的 false/true，确认停止与恢复；断开并恢复网络，确认超时后自动重连。
6. 设置 `timestamp_source:=camera`，确认收到 RTCP 时间基准前不发布帧，收到后与摄像机 NTP 时间一致；关闭摄像机 NTP 时验证持续警告而无错误时间戳帧。

在板端记录各项命令、实际输出和摄像机型号后，更新本节的验收结果。当前尚无 S100 实机结果。
