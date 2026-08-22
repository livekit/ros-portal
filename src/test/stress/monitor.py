#!/usr/bin/env python3
# Copyright 2026 LiveKit
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

"""Collect local process and ROS Portal diagnostic samples without psutil."""

import argparse
import json
import os
import subprocess
import time
from pathlib import Path

import rclpy
from diagnostic_msgs.msg import DiagnosticArray
from rclpy.node import Node


class Diagnostics(Node):
    def __init__(self) -> None:
        super().__init__('ros_portal_stress_monitor')
        self.values: dict[str, str] = {}
        self.create_subscription(DiagnosticArray, '/diagnostics', self._on_diagnostics, 10)

    def _on_diagnostics(self, message: DiagnosticArray) -> None:
        for status in message.status:
            if status.name.endswith('connection_health'):
                self.values = {value.key: value.value for value in status.values}


def process_sample(pid: int) -> dict:
    """Use POSIX ps because it is present in the ROS devcontainer and macOS."""
    try:
        output = subprocess.check_output(['ps', '-o', 'rss=', '-o', 'pcpu=', '-p', str(pid)], text=True).split()
    except subprocess.CalledProcessError:
        return {'missing': True}
    if len(output) != 2:
        return {'missing': True}
    return {'rss_kib': int(output[0]), 'cpu_percent': float(output[1])}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--portal-pid', type=int, required=True)
    parser.add_argument('--duration-s', type=float, required=True)
    parser.add_argument('--interval-s', type=float, default=1)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rclpy.init()
    node = Diagnostics()
    deadline = time.monotonic() + args.duration_s
    next_sample = time.monotonic()
    with args.output.open('w') as output:
        try:
            while time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.05)
                sample = {'unix_time_s': time.time(), 'host_load_1m': os.getloadavg()[0],
                          'portal': process_sample(args.portal_pid), 'portal_diagnostics': node.values}
                output.write(json.dumps(sample) + '\n')
                output.flush()
                next_sample += args.interval_s
                now = time.monotonic()
                while next_sample <= now:
                    next_sample += args.interval_s
                time.sleep(next_sample - now)
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
