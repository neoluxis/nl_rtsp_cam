---
author: N. C. Lee
created on: 2026-10-01
updated on: 2026-10-01
version: 0.1.0
title: nl_rtsp_cam 实施计划
tags:
  - Development Plan
  - ROS 2
---

# nl_rtsp_cam 实施计划

## 更新记录

| 日期 | 版本 | 作者 | 说明 |
| --- | --- | --- | --- |
| 2026-10-01 | 0.1.0 | N. C. Lee | 归档经确认的 ROS 包实施计划。 |

## 目标

为 RDK S100 + TROS Humble 实现单路 RTSP ROS 2 节点：FFmpeg 拆流，S100 `libmm` 硬解 H.264/H.265/MJPEG，默认发布 NV12；可选 JPEG 硬编和 hbmem。节点提供 `set_capture`、断线重连、标定信息及 `receive|camera` 时间戳。

## 工作项

1. 补齐参数验证、NV12 布局和 RTCP/NTP 时间换算，并以 GTest 验证。
2. 封装 S100 视频解码器与 JPEG 编码器，正确归还硬件缓冲区。
3. 实现单路 FFmpeg RTSP 工作线程、ROS 话题和 `set_capture` 服务；断流按配置间隔重连。
4. 增加标准启动和 Web 预览 launch，补齐包元数据、许可证和中英文使用说明。
5. 在 Orb Ubuntu 中构建并运行 CTest；在 S100 板端对三种编码、时间戳、服务和断流恢复进行实机验收。

## 验收边界

每个节点只连接一条流，默认 TCP、NV12 和本机接收时间，最高输入分辨率 1920×1080。选择相机时间时，由摄像机提供已同步的 NTP/RTCP 时间基准；缺少基准时不发布帧。全部文档位于本 ROS 包内。
