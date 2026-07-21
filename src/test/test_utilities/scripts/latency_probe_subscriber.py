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

"""Turns bridge latency probes into live percentile stats on the ROS graph.

Runs on the receiving ROS graph and subscribes to the bridge's republished
latency probes (ros_portal_msgs/LatencyTimestamps on
/ros_portal/latency/timestamp_rx). Each probe already carries T0..T4 in
its content; this node stamps T5 on arrival and derives the per-segment latency:

  t0_t1  publisher -> sending bridge (DDS)      t3_t4  bridge recv overhead
  t1_t2  bridge send overhead                   t4_t5  receiving bridge -> subscriber (DDS)
  t2_t3  LiveKit transport                      e2e    T5 - T0 (end to end)
  bridge_internal = t1_t2 + t3_t4  (the bridge's own added latency)

Every stats_period_sec it publishes a ros_portal_msgs/LatencyStats on
stats_topic (default /ros_portal/latency/stats) with p50/p90/p95/p99/
min/max/mean per segment (in milliseconds) over a rolling window. Percentiles are
precomputed, so the topic can be viewed live (ros2 topic echo / rqt_plot) and
bagged with no offline math. The publisher is latched (transient-local), so a
late subscriber immediately gets the most recent summary.

All stamps use the ROS clock; the two bridges must share a wall clock (e.g. run
on one host) for the cross-bridge segments to be comparable.

Parameters (set with --ros-args -p name:=value):
  rx_topic         Republished probe topic (default
                   /ros_portal/latency/timestamp_rx).
  stats_topic      Where LatencyStats is published (default
                   /ros_portal/latency/stats).
  stats_period_sec Seconds between published summaries (default 1.0).
  window_size      Rolling window per segment in samples (default 50; 0 = keep
                   all samples).
  warmup           Samples to skip per segment before counting (default 20), so
                   lazy LiveKit track setup on the first probes is excluded.
"""

from collections import deque

import math

import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile

from ros_portal_msgs.msg import LatencyMetric, LatencyStats, LatencyTimestamps

# Segments reported, in display order.
SEGMENTS = ['e2e', 'bridge_internal', 't0_t1', 't1_t2', 't2_t3', 't3_t4', 't4_t5']


def percentile(sorted_values, pct):
    """Nearest-rank percentile of an already-sorted, non-empty list."""
    rank = max(1, math.ceil(pct / 100.0 * len(sorted_values)))
    return sorted_values[min(rank, len(sorted_values)) - 1]


def is_set(stamp):
    return stamp.sec != 0 or stamp.nanosec != 0


def delta_ms(start, end):
    """(end - start) in milliseconds for two builtin_interfaces/Time stamps."""
    return (rclpy.time.Time.from_msg(end) - rclpy.time.Time.from_msg(start)).nanoseconds / 1e6


class RollingMetric:
    """A named rolling window of latency samples with a warmup skip."""

    def __init__(self, name, window_size, warmup):
        self.name = name
        self.warmup = warmup
        self.seen = 0
        self.samples = deque(maxlen=window_size if window_size > 0 else None)

    def add(self, value):
        self.seen += 1
        if self.seen > self.warmup:
            self.samples.append(value)

    def to_msg(self):
        ordered = sorted(self.samples)
        n = len(ordered)
        metric = LatencyMetric()
        metric.name = self.name
        metric.count = n
        if n == 0:
            return metric
        metric.p50_ms = percentile(ordered, 50)
        metric.p90_ms = percentile(ordered, 90)
        metric.p95_ms = percentile(ordered, 95)
        metric.p99_ms = percentile(ordered, 99)
        metric.min_ms = ordered[0]
        metric.max_ms = ordered[-1]
        metric.mean_ms = sum(ordered) / n
        return metric


class LatencyProbeSubscriber(Node):

    def __init__(self):
        super().__init__('latency_probe_subscriber')

        self.rx_topic = self.declare_parameter(
            'rx_topic', '/ros_portal/latency/timestamp_rx').value
        self.stats_topic = self.declare_parameter(
            'stats_topic', '/ros_portal/latency/stats').value
        self.stats_period_sec = float(self.declare_parameter('stats_period_sec', 1.0).value)
        window_size = int(self.declare_parameter('window_size', 50).value)
        warmup = int(self.declare_parameter('warmup', 20).value)

        self.metrics = {name: RollingMetric(name, window_size, warmup) for name in SEGMENTS}
        self.dropped = 0
        self.last_seq = None

        # Latch the stats so a late subscriber (ros2 topic echo, a dashboard)
        # immediately gets the most recent summary.
        stats_qos = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)

        self.create_subscription(LatencyTimestamps, self.rx_topic, self.on_probe, 10)
        self.stats_pub = self.create_publisher(LatencyStats, self.stats_topic, stats_qos)
        self.timer = self.create_timer(self.stats_period_sec, self.publish_stats)

        self.get_logger().info(
            'Reading latency probes from {}; publishing stats on {}'.format(self.rx_topic, self.stats_topic))

    def on_probe(self, msg: LatencyTimestamps):
        # T5: the far-side subscriber received the republished probe.
        msg.t5 = self.get_clock().now().to_msg()

        if is_set(msg.t0) and is_set(msg.t1):
            self.metrics['t0_t1'].add(delta_ms(msg.t0, msg.t1))
        if is_set(msg.t1) and is_set(msg.t2):
            self.metrics['t1_t2'].add(delta_ms(msg.t1, msg.t2))
        if is_set(msg.t2) and is_set(msg.t3):
            self.metrics['t2_t3'].add(delta_ms(msg.t2, msg.t3))
        if is_set(msg.t3) and is_set(msg.t4):
            self.metrics['t3_t4'].add(delta_ms(msg.t3, msg.t4))
        if is_set(msg.t4):
            self.metrics['t4_t5'].add(delta_ms(msg.t4, msg.t5))
        if is_set(msg.t0):
            self.metrics['e2e'].add(delta_ms(msg.t0, msg.t5))
        if is_set(msg.t1) and is_set(msg.t2) and is_set(msg.t3) and is_set(msg.t4):
            self.metrics['bridge_internal'].add(delta_ms(msg.t1, msg.t2) + delta_ms(msg.t3, msg.t4))

        if self.last_seq is not None and msg.seq > self.last_seq + 1:
            self.dropped += msg.seq - self.last_seq - 1
        self.last_seq = msg.seq

    def publish_stats(self):
        stats = LatencyStats()
        stats.header.stamp = self.get_clock().now().to_msg()
        # Fixed segment order keeps Foxglove/rqt_plot index paths stable (metrics[0]=e2e, ...).
        stats.metrics = [self.metrics[name].to_msg() for name in SEGMENTS]
        self.stats_pub.publish(stats)

        if not any(m.count for m in stats.metrics):
            self.get_logger().info('latency: no probes yet')
            return
        by_name = {m.name: m for m in stats.metrics}
        highlights = [n for n in ('e2e', 'bridge_internal', 't2_t3') if n in by_name]
        summary = '  '.join(
            '{} p50={:.3f} p95={:.3f} (n={})'.format(n, by_name[n].p50_ms, by_name[n].p95_ms, by_name[n].count)
            for n in highlights)
        self.get_logger().info('latency ms  dropped={}  {}'.format(self.dropped, summary))


def main(args=None):
    rclpy.init(args=args)
    node = LatencyProbeSubscriber()
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
