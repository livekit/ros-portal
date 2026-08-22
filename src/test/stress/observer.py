#!/usr/bin/env python3
# Copyright 2026 LiveKit
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

"""LiveKit-only delivery observer; it deliberately does not join the ROS graph."""

import argparse
import asyncio
import json
import statistics
import struct
import time
from collections import defaultdict
from pathlib import Path

from livekit import rtc


def decode_string_cdr(payload: bytes) -> dict | None:
    """Decode the simple CDR layout of std_msgs/msg/String used by telemetry."""
    if len(payload) < 8:
        return None
    length = struct.unpack_from('<I', payload, 4)[0]
    if length == 0 or len(payload) < 8 + length:
        return None
    try:
        return json.loads(payload[8:8 + length - 1].decode('utf-8'))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None


def percentile(values: list[float], fraction: float) -> float:
    return values[round((len(values) - 1) * fraction)]


def summary(values: list[float]) -> dict:
    if not values:
        return {}
    values.sort()
    return {'min': values[0], 'mean': statistics.fmean(values), 'p50': percentile(values, .50),
            'p95': percentile(values, .95), 'p99': percentile(values, .99), 'max': values[-1]}


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', required=True)
    parser.add_argument('--token', required=True)
    parser.add_argument('--duration-s', type=float, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--live-output', type=Path, required=True)
    args = parser.parse_args()
    room = rtc.Room()
    received = defaultdict(int)
    bytes_received = defaultdict(int)
    sequences: list[int] = []
    latencies_ms: list[float] = []
    video_frames = defaultdict(int)
    video_tasks: list[asyncio.Task] = []
    service_calls = 0
    measuring = False
    telemetry_frames_window = 0
    telemetry_latencies_window: list[float] = []
    video_frames_window = 0

    async def read_track(track: rtc.RemoteDataTrack) -> None:
        nonlocal measuring, telemetry_frames_window
        stream = track.subscribe(buffer_size=512)
        while frame := await stream.read():
            received[track.info.name] += 1
            bytes_received[track.info.name] += len(frame.payload)
            if track.info.name != '/stress/telemetry':
                continue
            sample = decode_string_cdr(frame.payload)
            if sample is not None and sample.get('phase') == 'measure':
                measuring = True
                sequences.append(sample['sequence'])
                latency_ms = (time.time_ns() - sample['sent_unix_ns']) / 1_000_000
                latencies_ms.append(latency_ms)
                telemetry_latencies_window.append(latency_ms)
                telemetry_frames_window += 1

    async def read_video(track: rtc.RemoteVideoTrack) -> None:
        nonlocal video_frames_window
        stream = rtc.VideoStream(track, capacity=2)
        async for _ in stream:
            if measuring:
                video_frames[track.sid] += 1
                video_frames_window += 1

    async def write_live_stats() -> None:
        nonlocal telemetry_frames_window, video_frames_window
        loop = asyncio.get_running_loop()
        next_update = loop.time() + 1
        while True:
            await asyncio.sleep(max(0, next_update - loop.time()))
            window = telemetry_latencies_window[:]
            telemetry_latencies_window.clear()
            stats = {
                'telemetry_frames_per_s': telemetry_frames_window,
                'telemetry_latency_avg_ms': statistics.fmean(window) if window else None,
                'video_frames_per_s': video_frames_window,
            }
            temporary = args.live_output.with_suffix('.tmp')
            temporary.write_text(json.dumps(stats))
            temporary.replace(args.live_output)
            telemetry_frames_window = 0
            video_frames_window = 0
            next_update += 1
            while next_update <= loop.time():
                next_update += 1

    @room.on('data_track_published')
    def on_data_track(track: rtc.RemoteDataTrack) -> None:
        video_tasks.append(asyncio.create_task(read_track(track)))

    @room.on('track_subscribed')
    def on_track(track, publication, participant) -> None:
        if isinstance(track, rtc.RemoteVideoTrack):
            video_tasks.append(asyncio.create_task(read_video(track)))

    await room.connect(args.url, args.token, rtc.RoomOptions(auto_subscribe=True))
    @room.local_participant.register_rpc_method('ros2_service_call')
    async def on_service_call(invocation: rtc.RpcInvocationData) -> str:
        nonlocal service_calls
        service_calls += 1
        return json.dumps({'success': True, 'err_msg': '', 'output': 'success: true'})

    live_task = asyncio.create_task(write_live_stats())
    try:
        await asyncio.sleep(args.duration_s)
    finally:
        await room.disconnect()
        live_task.cancel()
        await asyncio.gather(live_task, return_exceptions=True)
        for task in video_tasks:
            task.cancel()
        await asyncio.gather(*video_tasks, return_exceptions=True)
        unique = set(sequences)
        missing = (max(unique) - min(unique) + 1 - len(unique)) if unique else 0
        args.output.write_text(json.dumps({
            'data_frames_received': dict(received), 'data_bytes_received': dict(bytes_received),
            'telemetry_unique_sequences': len(unique), 'telemetry_missing_sequences': missing,
            'telemetry_duplicate_sequences': len(sequences) - len(unique), 'telemetry_latency_ms': summary(latencies_ms),
            'video_frames_received': sum(video_frames.values()), 'service_rpc_calls_received': service_calls,
        }, indent=2) + '\n')


if __name__ == '__main__':
    asyncio.run(main())
