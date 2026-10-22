# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""rosbag2 writer for the ScaRF-SLAM input bag."""

from pathlib import Path

from livekit_scarf_slam.slam_bag import FINAL_TRAJECTORY_TOPIC
from livekit_scarf_slam.slam_bag import IMAGE_TOPIC
from livekit_scarf_slam.slam_bag import ODOMETRY_TOPIC
from livekit_scarf_slam.slam_bag import TRAJECTORY_TOPIC
from rclpy.serialization import serialize_message
import rosbag2_py

_TOPIC_TYPES = (
    (TRAJECTORY_TOPIC, 'nav_msgs/msg/Path'),
    (FINAL_TRAJECTORY_TOPIC, 'nav_msgs/msg/Path'),
    (ODOMETRY_TOPIC, 'nav_msgs/msg/Odometry'),
    (IMAGE_TOPIC, 'sensor_msgs/msg/CompressedImage'),
)


class SlamBagWriter:
    """Write the four ScaRF-SLAM input topics to a new bag."""

    def __init__(self, uri, storage_id='mcap'):
        if Path(uri).exists():
            raise FileExistsError(f'bag path already exists: {uri}')
        Path(uri).parent.mkdir(parents=True, exist_ok=True)
        self._writer = rosbag2_py.SequentialWriter()
        self._writer.open(
            rosbag2_py.StorageOptions(uri=str(uri), storage_id=storage_id),
            rosbag2_py.ConverterOptions('cdr', 'cdr'))
        for topic_id, (name, msg_type) in enumerate(_TOPIC_TYPES):
            self._writer.create_topic(rosbag2_py.TopicMetadata(
                id=topic_id, name=name, type=msg_type, serialization_format='cdr'))

    def write(self, topic, msg, stamp_ns):
        """Write one message with the receive time stamp_ns."""
        self._writer.write(topic, serialize_message(msg), stamp_ns)

    def close(self):
        """Flush and close the bag."""
        self._writer.close()
