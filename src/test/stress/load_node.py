#!/usr/bin/env python3
# Copyright 2026 LiveKit
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

"""ROS-side workload for the developer stress harness."""

import argparse
import json
import statistics
import time
from pathlib import Path

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from std_msgs.msg import String
from std_srvs.srv import Trigger


GLYPHS = {
    'A': ('01110', '10001', '10001', '11111', '10001', '10001', '10001'),
    'E': ('11111', '10000', '11110', '10000', '10000', '10000', '11111'),
    'I': ('11111', '00100', '00100', '00100', '00100', '00100', '11111'),
    'K': ('10001', '10010', '10100', '11000', '10100', '10010', '10001'),
    'L': ('10000', '10000', '10000', '10000', '10000', '10000', '11111'),
    'O': ('01110', '10001', '10001', '10001', '10001', '10001', '01110'),
    'P': ('11110', '10001', '10001', '11110', '10000', '10000', '10000'),
    'R': ('11110', '10001', '10001', '11110', '10100', '10010', '10001'),
    'S': ('01111', '10000', '10000', '01110', '00001', '00001', '11110'),
    'T': ('11111', '00100', '00100', '00100', '00100', '00100', '00100'),
}


class LoadNode(Node):
    def __init__(self, args: argparse.Namespace) -> None:
        super().__init__('ros_portal_stress_load')
        self._args = args
        self._started_at: float | None = None
        self._sequence = 0
        self._published = 0
        self._image_published = 0
        self._service_latencies_ms: list[float] = []
        self._service_failures = 0
        self._service_in_flight = False
        self._measuring = False
        self._frame_index = 0
        self._image_background = bytes((16, 18, 24)) * (args.camera_width * args.camera_height)
        self._telemetry = self.create_publisher(String, '/stress/telemetry', 10)
        self._image = self.create_publisher(Image, '/stress/camera/image_raw', 2)
        self._client = self.create_client(Trigger, '/stress/echo')
        # Publish during warmup so ROS Portal discovers the topics and creates
        # their LiveKit tracks before the measured sequence begins.
        self._timers = [
            self.create_timer(1.0 / args.telemetry_hz, self._publish_telemetry),
            self.create_timer(1.0 / args.camera_fps, self._publish_image),
            self.create_timer(1.0 / args.service_hz, self._call_service),
        ]

    def start(self) -> None:
        """Begin measured publishing after ROS Portal has discovered the endpoints."""
        self._started_at = time.monotonic()
        self._sequence = 0
        self._published = 0
        self._image_published = 0
        self._measuring = True

    def _publish_telemetry(self) -> None:
        message = String()
        # time.time_ns permits clock-free latency calculation in the independent
        # LiveKit observer when both processes run on the same developer host.
        message.data = json.dumps({'phase': 'measure' if self._measuring else 'warmup',
                                   'sequence': self._sequence, 'sent_unix_ns': time.time_ns(),
                                   'padding': 'x' * self._args.telemetry_padding_bytes}, separators=(',', ':'))
        self._telemetry.publish(message)
        self._sequence += 1
        if self._measuring:
            self._published += 1

    def _publish_image(self) -> None:
        width, height = self._args.camera_width, self._args.camera_height
        message = Image()
        message.height = height
        message.width = width
        message.encoding = 'bgr8'
        message.step = width * 3
        message.data = self._bouncing_logo_frame(width, height)
        self._image.publish(message)
        self._frame_index += 1
        if self._measuring:
            self._image_published += 1

    @staticmethod
    def _bounce_position(frame: int, distance: int, speed: int) -> int:
        if distance <= 0:
            return 0
        position = (frame * speed) % (distance * 2)
        return position if position <= distance else distance * 2 - position

    @staticmethod
    def _fill_rect(frame: bytearray, width: int, x: int, y: int, rect_width: int, rect_height: int,
                   color: tuple[int, int, int]) -> None:
        for row in range(y, y + rect_height):
            start = (row * width + x) * 3
            frame[start:start + rect_width * 3] = bytes(color) * rect_width

    def _draw_text(self, frame: bytearray, width: int, x: int, y: int, scale: int,
                   color: tuple[int, int, int]) -> None:
        cursor_x = x
        for character in 'ROS PORTAL':
            if character == ' ':
                cursor_x += 3 * scale
                continue
            for glyph_y, row in enumerate(GLYPHS[character]):
                for glyph_x, pixel in enumerate(row):
                    if pixel == '1':
                        self._fill_rect(frame, width, cursor_x + glyph_x * scale, y + glyph_y * scale,
                                        scale, scale, color)
            cursor_x += 6 * scale

    def _bouncing_logo_frame(self, width: int, height: int) -> bytes:
        """Render a DVD-style bouncing ROS Portal logo into a BGR image frame."""
        scale = max(1, min(4, width // 160, height // 80))
        text_width = 57 * scale
        logo_width = text_width + 8 * scale
        logo_height = 15 * scale
        x = self._bounce_position(self._frame_index, width - logo_width, 3)
        y = self._bounce_position(self._frame_index, height - logo_height, 2)
        frame = bytearray(self._image_background)
        accent = (255, 184, 0) if (self._frame_index // 90) % 2 == 0 else (236, 86, 123)
        self._fill_rect(frame, width, x, y, logo_width, logo_height, accent)
        self._fill_rect(frame, width, x + scale, y + scale, logo_width - 2 * scale, logo_height - 2 * scale,
                        (30, 31, 38))
        self._draw_text(frame, width, x + 4 * scale, y + 4 * scale, scale, accent)
        return bytes(frame)

    def _call_service(self) -> None:
        if not self._measuring or not self._client.service_is_ready() or self._service_in_flight:
            return
        started = time.monotonic_ns()
        self._service_in_flight = True
        future = self._client.call_async(Trigger.Request())

        def done(result) -> None:
            try:
                response = result.result()
                if not response.success:
                    self._service_failures += 1
                    return
                self._service_latencies_ms.append((time.monotonic_ns() - started) / 1_000_000)
            except Exception:  # The report counts failed calls; the exception is not actionable here.
                self._service_failures += 1
            finally:
                self._service_in_flight = False

        future.add_done_callback(done)

    def summary(self) -> dict:
        latencies = sorted(self._service_latencies_ms)
        return {
            'duration_s': time.monotonic() - self._started_at if self._started_at else 0,
            'telemetry_published': self._published,
            'images_published': self._image_published,
            'service_calls_succeeded': len(latencies),
            'service_calls_failed': self._service_failures,
            'service_latency_ms': percentile_summary(latencies),
        }


def percentile_summary(values: list[float]) -> dict:
    if not values:
        return {}
    return {'min': values[0], 'mean': statistics.fmean(values), 'p50': percentile(values, .50),
            'p95': percentile(values, .95), 'p99': percentile(values, .99), 'max': values[-1]}


def percentile(values: list[float], fraction: float) -> float:
    return values[round((len(values) - 1) * fraction)]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--duration-s', type=float, required=True)
    parser.add_argument('--telemetry-hz', type=float, default=100)
    parser.add_argument('--telemetry-padding-bytes', type=int, default=192)
    parser.add_argument('--camera-fps', type=float, default=30)
    parser.add_argument('--camera-width', type=int, default=640)
    parser.add_argument('--camera-height', type=int, default=480)
    parser.add_argument('--service-hz', type=float, default=1)
    parser.add_argument('--warmup-s', type=float, default=5,
                        help='Unmeasured discovery interval before messages are published.')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rclpy.init()
    node = LoadNode(args)
    try:
        warmup_deadline = time.monotonic() + args.warmup_s
        while rclpy.ok() and time.monotonic() < warmup_deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
        node.start()
        deadline = time.monotonic() + args.duration_s
        while rclpy.ok() and time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
    finally:
        args.output.write_text(json.dumps(node.summary(), indent=2) + '\n')
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
