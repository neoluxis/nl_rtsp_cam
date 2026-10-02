# 使用 JPEG 模式启动 RTSP 摄像头及 Web 预览。
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    camera_launch = os.path.join(
        get_package_share_directory("nl_rtsp_cam"), "launch", "nl_rtsp_cam.launch.py"
    )
    websocket_launch = os.path.join(
        get_package_share_directory("websocket"), "launch", "websocket.launch.py"
    )
    return LaunchDescription(
        [
            DeclareLaunchArgument("rtsp_url", description="RTSP stream URL"),
            DeclareLaunchArgument("rtsp_transport", default_value="tcp"),
            DeclareLaunchArgument("timestamp_source", default_value="receive"),
            DeclareLaunchArgument("frame_id", default_value="rtsp_cam"),
            DeclareLaunchArgument("image_topic", default_value="/image"),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(camera_launch),
                launch_arguments={
                    "rtsp_url": LaunchConfiguration("rtsp_url"),
                    "rtsp_transport": LaunchConfiguration("rtsp_transport"),
                    "timestamp_source": LaunchConfiguration("timestamp_source"),
                    "frame_id": LaunchConfiguration("frame_id"),
                    "pixel_format": "jpeg",
                    "zero_copy": "false",
                }.items(),
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(websocket_launch),
                launch_arguments={
                    "websocket_image_topic": LaunchConfiguration("image_topic"),
                    "websocket_only_show_image": "True",
                }.items(),
            ),
        ]
    )
