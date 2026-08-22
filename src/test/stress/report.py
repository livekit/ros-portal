#!/usr/bin/env python3
# Copyright 2026 LiveKit
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

"""Render a concise, human-readable stress-test report from harness artifacts."""

import argparse
import json
import statistics
from pathlib import Path


def load_json(path: Path) -> dict:
    return json.loads(path.read_text())


def read_samples(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text().splitlines() if line]


def render_number(value, suffix='') -> str:
    return 'n/a' if value is None else f'{value:.2f}{suffix}' if isinstance(value, float) else f'{value}{suffix}'


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-dir', type=Path, required=True)
    args = parser.parse_args()
    load = load_json(args.run_dir / 'load.json')
    observer = load_json(args.run_dir / 'observer.json')
    samples = read_samples(args.run_dir / 'monitor.jsonl')
    process = [sample['portal'] for sample in samples if not sample['portal'].get('missing')]
    rss = [sample['rss_kib'] / 1024 for sample in process]
    cpu = [sample['cpu_percent'] for sample in process]
    diagnostics = [sample['portal_diagnostics'] for sample in samples if sample['portal_diagnostics']]
    # Diagnostic tasks can be published independently. Prefer the latest
    # connection-health snapshot that includes a complete RTC stats sample over
    # a later non-RTC task update.
    latest = next((sample for sample in reversed(diagnostics) if sample.get('rtc.stats_available') == 'true'), {})
    latency = observer['telemetry_latency_ms']
    service = load['service_latency_ms']
    report = f'''# ROS Portal stress-test report

## Workload

| Metric | Result |
|---|---:|
| Duration | {render_number(load['duration_s'], ' s')} |
| Telemetry published | {load['telemetry_published']} |
| Image frames published | {load['images_published']} |
| Service calls (success / failed) | {load['service_calls_succeeded']} / {load['service_calls_failed']} |

## Delivery and latency

| Metric | Result |
|---|---:|
| Telemetry unique / missing / duplicate | {observer['telemetry_unique_sequences']} / {observer['telemetry_missing_sequences']} / {observer['telemetry_duplicate_sequences']} |
| Telemetry latency p50 / p95 / p99 | {render_number(latency.get('p50'), ' ms')} / {render_number(latency.get('p95'), ' ms')} / {render_number(latency.get('p99'), ' ms')} |
| Video frames received | {observer['video_frames_received']} |
| Observer service RPC calls | {observer['service_rpc_calls_received']} |
| Service latency p50 / p95 / p99 | {render_number(service.get('p50'), ' ms')} / {render_number(service.get('p95'), ' ms')} / {render_number(service.get('p99'), ' ms')} |

## Resource and transport observations

| Metric | Result |
|---|---:|
| ROS Portal RSS max / growth | {render_number(max(rss) if rss else None, ' MiB')} / {render_number((max(rss) - min(rss)) if rss else None, ' MiB')} |
| ROS Portal CPU average / max | {render_number(statistics.fmean(cpu) if cpu else None, '%')} / {render_number(max(cpu) if cpu else None, '%')} |
| Host 1-minute load max | {render_number(max((sample['host_load_1m'] for sample in samples), default=None))} |
| LiveKit RTT / packet loss / max jitter | {latest.get('rtc.transport.current_round_trip_time_ms', 'n/a')} ms / {latest.get('rtc.traffic.packets_lost', 'n/a')} / {latest.get('rtc.traffic.max_jitter_ms', 'n/a')} ms |
| LiveKit send bitrate | {latest.get('rtc.traffic.send_bitrate_bps', 'n/a')} bps |
'''
    (args.run_dir / 'report.md').write_text(report)


if __name__ == '__main__':
    main()
