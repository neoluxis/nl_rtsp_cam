---
author: N. C. Lee
created on: 2026-10-01
updated on: 2026-10-03
version: 0.4.0
title: S100 RTSP Camera ROS 2 Package
tags:
  - ROS 2
  - RTSP
---

# S100 RTSP Camera ROS 2 Package

## Change history

| Date | Version | Author | Change |
| --- | --- | --- | --- |
| 2026-10-03 | 0.4.0 | N. C. Lee | Added subscriber-gated standard ROS images alongside shared-memory inference images. |
| 2026-10-03 | 0.3.0 | N. C. Lee | Smoothed publication rate limiting for multi-camera input. |
| 2026-10-03 | 0.2.0 | N. C. Lee | Added source frame metadata for multi-camera detection. |
| 2026-10-01 | 0.1.0 | N. C. Lee | Added single-stream RTSP capture, S100 decoding, JPEG output, and launch files. |

`nl_rtsp_cam` connects one RTSP stream per node. FFmpeg receives H.264, H.265, or MJPEG video; RDK S100 `libmm` decodes it to NV12. The default `image` topic carries `sensor_msgs/Image`. Set `pixel_format:=jpeg` to publish `sensor_msgs/CompressedImage`, or `zero_copy:=true` to publish `HbmMsg1080P` on `hbmem_img`. The supported input limit is 1920×1080.

Each published frame also has a `frame_metadata` message (`nl_image_msgs/FrameMetadata`). Set `node_id` and `camera_id` per instance. Its stamp and frame index match the source image; in shared-memory mode the index matches `HbmMsg1080P.index`.

With `zero_copy:=true`, set `publish_ros_image:=true` to also advertise a standard NV12 `sensor_msgs/Image` topic. It copies frames only while subscribed; `ros_image_fps` limits this secondary stream. Match a standard image to `frame_metadata` by camera namespace and header stamp.

`framerate` limits the average published frame rate. A two-frame allowance absorbs small arrival jitter so a 24 FPS source configured at 24 FPS is not systematically reduced.

Build in TROS Humble with FFmpeg development packages and `hobot-multimedia-dev`:

```bash
source /opt/tros/humble/setup.bash
colcon build --base-paths /Users/neolux/Projects/dk2026003/ai_service/nl_rtsp_cam \
  --packages-select nl_rtsp_cam
source install/setup.bash
ros2 launch nl_rtsp_cam nl_rtsp_cam.launch.py \
  rtsp_url:='rtsp://user:password@camera/stream'
```

Use `nl_rtsp_cam_websocket.launch.py` for JPEG Web preview. Supply a distinct `namespace` for each camera. The `set_capture` SetBool service pauses capture when false and reconnects when true. Connection failures automatically retry after `reconnect_delay_ms` (default 2000). Do not store credentials in repository launch files.

The default `timestamp_source=receive` stamps frames on the node. `timestamp_source=camera` uses RTCP/NTP sender reports plus video PTS; frames wait until a valid absolute time base arrives. A valid ROS calibration YAML can be supplied through `camera_calibration_file_path` to publish synchronized `camera_info`.

Available parameters: `rtsp_url` (required), `rtsp_transport=tcp|udp`, `pixel_format=nv12|jpeg`, `zero_copy=false`, `timestamp_source=receive|camera`, `frame_id=rtsp_cam`, `camera_calibration_file_path`, `framerate=0` (unlimited), `bitstream_buffer_bytes=8388608` (1–64 MiB), `connect_timeout_ms=5000`, `read_timeout_ms=5000`, and `reconnect_delay_ms=2000`.

See the Chinese guide[^chinese] for the full topic table and the test guide[^testing] for validation steps and results.

[^chinese]: [S100 RTSP 摄像头 ROS 2 包](README_cn.md)
[^testing]: [nl_rtsp_cam 测试与板端验收](docs/testing.md)
