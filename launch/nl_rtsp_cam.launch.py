# 启动一条 RTSP 流的 S100 图像节点。
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, SetEnvironmentVariable
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import EnvironmentVariable, LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    arguments = [
        DeclareLaunchArgument("rtsp_url", description="RTSP stream URL"),
        DeclareLaunchArgument("namespace", default_value=""),
        DeclareLaunchArgument("rtsp_transport", default_value="tcp"),
        DeclareLaunchArgument("pixel_format", default_value="nv12"),
        DeclareLaunchArgument("timestamp_source", default_value="receive"),
        DeclareLaunchArgument("frame_id", default_value="rtsp_cam"),
        DeclareLaunchArgument("camera_calibration_file_path", default_value=""),
        DeclareLaunchArgument("connect_timeout_ms", default_value="5000"),
        DeclareLaunchArgument("read_timeout_ms", default_value="5000"),
        DeclareLaunchArgument("reconnect_delay_ms", default_value="2000"),
        DeclareLaunchArgument("framerate", default_value="0"),
        DeclareLaunchArgument("bitstream_buffer_bytes", default_value="8388608"),
        DeclareLaunchArgument("zero_copy", default_value="false"),
        DeclareLaunchArgument("log_level", default_value="info"),
    ]
    shm_launch = os.path.join(
        get_package_share_directory("hobot_shm"), "launch", "hobot_shm.launch.py"
    )
    return LaunchDescription(
        arguments
        + [
            SetEnvironmentVariable(
                name="LD_LIBRARY_PATH",
                value=["/usr/hobot/lib:", EnvironmentVariable("LD_LIBRARY_PATH", default_value="")],
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(shm_launch),
                condition=IfCondition(LaunchConfiguration("zero_copy")),
            ),
            Node(
                package="nl_rtsp_cam",
                executable="nl_rtsp_cam",
                name="nl_rtsp_cam",
                namespace=LaunchConfiguration("namespace"),
                output="screen",
                parameters=[
                    {
                        "rtsp_url": LaunchConfiguration("rtsp_url"),
                        "rtsp_transport": LaunchConfiguration("rtsp_transport"),
                        "pixel_format": LaunchConfiguration("pixel_format"),
                        "timestamp_source": LaunchConfiguration("timestamp_source"),
                        "frame_id": LaunchConfiguration("frame_id"),
                        "camera_calibration_file_path": LaunchConfiguration(
                            "camera_calibration_file_path"
                        ),
                        "connect_timeout_ms": ParameterValue(
                            LaunchConfiguration("connect_timeout_ms"), value_type=int
                        ),
                        "read_timeout_ms": ParameterValue(
                            LaunchConfiguration("read_timeout_ms"), value_type=int
                        ),
                        "reconnect_delay_ms": ParameterValue(
                            LaunchConfiguration("reconnect_delay_ms"), value_type=int
                        ),
                        "framerate": ParameterValue(
                            LaunchConfiguration("framerate"), value_type=int
                        ),
                        "bitstream_buffer_bytes": ParameterValue(
                            LaunchConfiguration("bitstream_buffer_bytes"), value_type=int
                        ),
                        "zero_copy": ParameterValue(
                            LaunchConfiguration("zero_copy"), value_type=bool
                        ),
                    }
                ],
                arguments=["--ros-args", "--log-level", LaunchConfiguration("log_level")],
            ),
        ]
    )
