#!/usr/bin/env python3

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

"""Publish simulated LiveKit bridge diagnostics for demo purposes."""

from dataclasses import dataclass

from diagnostic_msgs.msg import DiagnosticStatus
import diagnostic_updater
import rclpy
from rclpy.node import Node


@dataclass(frozen=True)
class ConnectionSample:
    """One simulated connection-health diagnostic state."""

    level: int
    message: str
    connected: bool
    sfu_ping_ms: int
    num_peers: int
    reconnect_count: int


@dataclass(frozen=True)
class RosGraphSample:
    """One simulated ROS graph diagnostic state."""

    level: int
    message: str
    num_subbed_topics: int
    num_pub_topics: int
    num_services: int
    num_unmatched_topics: int


class SimulatedBridgeDiagnostics(Node):
    """Publish deterministic bridge diagnostic samples."""

    def __init__(self):
        """Create diagnostic tasks and the scenario timer."""
        super().__init__('lk_diagnostics')

        self.declare_parameter('scenario_period_sec', 5.0)
        scenario_period_sec = (
            self.get_parameter('scenario_period_sec').get_parameter_value()
            .double_value
        )

        self._scenario_index = 0
        self._room_name = 'diagnostics-poc-room'
        self._connection_samples = [
            ConnectionSample(
                level=DiagnosticStatus.OK,
                message='Connected to LiveKit SFU',
                connected=True,
                sfu_ping_ms=38,
                num_peers=3,
                reconnect_count=0,
            ),
            ConnectionSample(
                level=DiagnosticStatus.WARN,
                message='Connected, SFU ping is elevated',
                connected=True,
                sfu_ping_ms=245,
                num_peers=3,
                reconnect_count=1,
            ),
            ConnectionSample(
                level=DiagnosticStatus.ERROR,
                message='Disconnected from LiveKit room',
                connected=False,
                sfu_ping_ms=-1,
                num_peers=0,
                reconnect_count=2,
            ),
        ]
        self._ros_graph_samples = [
            RosGraphSample(
                level=DiagnosticStatus.ERROR,
                message='ROS graph discovery healthy',
                num_subbed_topics=8,
                num_pub_topics=13,
                num_services=6,
                num_unmatched_topics=0,
            ),
            RosGraphSample(
                level=DiagnosticStatus.ERROR,
                message='Some configured topics are unmatched',
                num_subbed_topics=5,
                num_pub_topics=10,
                num_services=6,
                num_unmatched_topics=3,
            ),
            RosGraphSample(
                level=DiagnosticStatus.ERROR,
                message='ROS graph recovered after reconnect',
                num_subbed_topics=7,
                num_pub_topics=12,
                num_services=6,
                num_unmatched_topics=0,
            ),
        ]

        self._updater = diagnostic_updater.Updater(self)
        self._updater.setHardwareID('simulated-ros2-livekit-bridge')
        self._updater.add('connection_health', self._connection_health)
        self._updater.add('ros_graph', self._ros_graph)

        self._scenario_timer = self.create_timer(
            scenario_period_sec,
            self._advance_scenario,
        )
        self._updater.force_update()

    def _advance_scenario(self):
        """Move to the next simulated state."""
        self._scenario_index += 1
        self._updater.force_update()

    def _connection_health(self, stat):
        """Populate the current connection-health diagnostic status."""
        sample = self._connection_samples[
            self._scenario_index % len(self._connection_samples)
        ]

        stat.summary(sample.level, sample.message)
        stat.add('connected', str(sample.connected).lower())
        stat.add('sfu_ping_ms', str(sample.sfu_ping_ms))
        stat.add('num_peers', str(sample.num_peers))
        stat.add('reconnect_count', str(sample.reconnect_count))
        stat.add('room_name', self._room_name)
        return stat

    def _ros_graph(self, stat):
        """Populate the current ROS graph diagnostic status."""
        sample = self._ros_graph_samples[
            self._scenario_index % len(self._ros_graph_samples)
        ]

        stat.summary(sample.level, sample.message)
        stat.add('num_subbed_topics', str(sample.num_subbed_topics))
        stat.add('num_pub_topics', str(sample.num_pub_topics))
        stat.add('num_services', str(sample.num_services))
        stat.add('num_unmatched_topics', str(sample.num_unmatched_topics))
        return stat


def main(args=None):
    """Run the simulated diagnostics node."""
    rclpy.init(args=args)
    node = SimulatedBridgeDiagnostics()

    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
