#!/usr/bin/env python3
# Copyright 2026 LiveKit
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

"""One-command, local ROS Portal stress-test supervisor."""

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
STRESS_DIR = ROOT / 'src' / 'test' / 'stress'
PORTAL_IDENTITY = 'ros-portal-stress'
OBSERVER_IDENTITY = 'ros-portal-stress-observer'


def mint_token(identity: str, room: str, api_key: str, api_secret: str) -> str:
    return subprocess.check_output(['lk', 'token', 'create', '--api-key', api_key, '--api-secret', api_secret,
                                    '--identity', identity, '--name', identity, '--join', '--room', room,
                                    '--valid-for', '2h', '--allow-update-metadata', '--token-only'], text=True).strip()


def descendants(root_pid: int) -> list[tuple[int, str]]:
    rows = []
    # GNU procps requires comma-separated fields for a BSD-style -o list;
    # macOS accepts this form too.
    for line in subprocess.check_output(['ps', '-axo', 'pid=,ppid=,command='], text=True).splitlines():
        fields = line.strip().split(None, 2)
        if len(fields) == 3 and fields[0].isdigit() and fields[1].isdigit():
            rows.append((int(fields[0]), int(fields[1]), fields[2]))
    known = {root_pid}
    while added := {pid for pid, parent, _ in rows if parent in known} - known:
        known.update(added)
    return [(pid, command) for pid, _, command in rows if pid in known]


def portal_pid(launch_pid: int, timeout_s: float) -> int:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        for pid, command in descendants(launch_pid):
            if 'ros_portal_node' in command:
                return pid
        time.sleep(.2)
    raise RuntimeError('ROS Portal node did not start; inspect portal.log')


def start(command: list[str], log_path: Path, env: dict[str, str]) -> subprocess.Popen:
    return subprocess.Popen(command, stdout=log_path.open('w'), stderr=subprocess.STDOUT,
                            env=env, start_new_session=True)


def stop(process: subprocess.Popen | None, stop_signal: signal.Signals = signal.SIGTERM) -> None:
    if process is None or process.poll() is not None:
        return
    os.killpg(process.pid, stop_signal)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def stop_pid(pid: int | None, stop_signal: signal.Signals = signal.SIGTERM) -> None:
    if pid is None:
        return
    try:
        os.kill(pid, stop_signal)
    except ProcessLookupError:
        pass


def read_latest_sample(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text().splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        return {}


def read_latest_rtc_diagnostics(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        samples = path.read_text().splitlines()
        for line in reversed(samples):
            diagnostics = json.loads(line).get('portal_diagnostics', {})
            if diagnostics.get('rtc.stats_available') == 'true':
                return diagnostics
    except json.JSONDecodeError:
        pass
    return {}


def read_json(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError:
        return {}


def print_live_stats(run_dir: Path, elapsed_s: float) -> None:
    sample = read_latest_sample(run_dir / 'monitor.jsonl')
    portal = sample.get('portal', {})
    diagnostics = read_latest_rtc_diagnostics(run_dir / 'monitor.jsonl')
    observer = read_json(run_dir / 'observer_live.json')
    rss = portal.get('rss_kib')
    rss_text = f'{rss / 1024:.1f} MiB' if rss is not None else 'n/a'
    latency = observer.get('telemetry_latency_avg_ms')
    latency_text = f'{latency:.2f} ms' if latency is not None else 'n/a'
    print(f't={elapsed_s:.1f}s '
          f'rss={rss_text} cpu={portal.get("cpu_percent", "n/a")}% '
          f'rtt={diagnostics.get("rtc.transport.current_round_trip_time_ms", "n/a")}ms '
          f'loss={diagnostics.get("rtc.traffic.packets_lost", "n/a")} '
          f'tx={diagnostics.get("rtc.traffic.send_bitrate_bps", "n/a")}bps | '
          f'telemetry={observer.get("telemetry_frames_per_s", "n/a")}/s avg={latency_text} '
          f'video={observer.get("video_frames_per_s", "n/a")}/s', flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration-s', type=float, default=600)
    parser.add_argument('--room', default=None, help='Optional fixed room name; defaults to a unique room per run.')
    parser.add_argument('--url', default=os.getenv('LIVEKIT_URL', 'ws://127.0.0.1:7880'))
    parser.add_argument('--api-key', default=os.getenv('LIVEKIT_API_KEY', 'devkey'))
    parser.add_argument('--api-secret', default=os.getenv('LIVEKIT_API_SECRET', 'secret'))
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'artifacts' / 'stress')
    parser.add_argument('--telemetry-hz', type=float, default=100)
    parser.add_argument('--camera-fps', type=float, default=30)
    parser.add_argument('--camera-width', type=int, default=640)
    parser.add_argument('--camera-height', type=int, default=480)
    parser.add_argument('--service-hz', type=float, default=1)
    parser.add_argument('--warmup-s', type=float, default=5)
    args = parser.parse_args()
    run_id = time.strftime('%Y%m%d-%H%M%S')
    if args.room is None:
        args.room = f'ros_portal_stress_{run_id}'
    for command in ('lk', 'ros2'):
        if shutil.which(command) is None:
            parser.error(f'{command} is required on PATH; source the ROS workspace and install the LiveKit CLI first')
    try:
        __import__('livekit.rtc')
    except ModuleNotFoundError:
        parser.error('the selected Python lacks livekit.rtc; create .venv-stress as documented and run '
                     '.venv-stress/bin/python src/test/stress/run.py')
    run_dir = args.output_dir / run_id
    run_dir.mkdir(parents=True)
    print('creating LiveKit tokens...', flush=True)
    portal_token = mint_token(PORTAL_IDENTITY, args.room, args.api_key, args.api_secret)
    observer_token = mint_token(OBSERVER_IDENTITY, args.room, args.api_key, args.api_secret)
    env = os.environ | {'LIVEKIT_URL': args.url}
    observer = portal = monitor = loader = None
    portal_node_pid = None
    interrupted = False
    try:
        print('starting LiveKit observer...', flush=True)
        observer = start([sys.executable, str(STRESS_DIR / 'observer.py'), '--url', args.url, '--token', observer_token,
                          '--duration-s', str(args.duration_s + args.warmup_s + 12), '--output', str(run_dir / 'observer.json'),
                          '--live-output', str(run_dir / 'observer_live.json')],
                         run_dir / 'observer.log', env)
        time.sleep(2)
        print('starting ROS Portal...', flush=True)
        portal = start(['ros2', 'launch', 'ros_portal', 'ros_portal_local.launch.py',
                        f'config_path:={STRESS_DIR / "stress_config.yaml"}', f'livekit_url:={args.url}',
                        f'identity:={PORTAL_IDENTITY}', f'token:={portal_token}'], run_dir / 'portal.log', env)
        portal_node_pid = portal_pid(portal.pid, 15)
        print(f'ROS Portal started (pid {portal_node_pid}); waiting for room connection...', flush=True)
        time.sleep(3)
        print(f'starting ROS workload; warming up tracks for {args.warmup_s:.1f}s...', flush=True)
        loader = start([sys.executable, str(STRESS_DIR / 'load_node.py'), '--duration-s', str(args.duration_s),
                        '--telemetry-hz', str(args.telemetry_hz), '--camera-fps', str(args.camera_fps),
                        '--camera-width', str(args.camera_width), '--camera-height', str(args.camera_height),
                        '--service-hz', str(args.service_hz), '--warmup-s', str(args.warmup_s),
                        '--output', str(run_dir / 'load.json')],
                       run_dir / 'load.log', env)
        # Exclude Portal startup and track creation from the resource trend.
        time.sleep(args.warmup_s)
        monitor = start([sys.executable, str(STRESS_DIR / 'monitor.py'), '--portal-pid', str(portal_node_pid),
                         '--duration-s', str(args.duration_s + 2), '--output', str(run_dir / 'monitor.jsonl')],
                        run_dir / 'monitor.log', env)
        test_started_at = time.monotonic()
        print('workload running; collecting live stats...', flush=True)
        next_live_update = test_started_at
        while loader.poll() is None:
            print_live_stats(run_dir, time.monotonic() - test_started_at)
            next_live_update += 1
            now = time.monotonic()
            while next_live_update <= now:
                next_live_update += 1
            time.sleep(next_live_update - now)
        if loader.returncode != 0:
            raise RuntimeError('load generator failed; inspect load.log')
        monitor.wait(timeout=8)
        observer.wait(timeout=15)
    except KeyboardInterrupt:
        interrupted = True
        print('interrupted, stopping test...', flush=True)
    finally:
        child_signal = signal.SIGINT if interrupted else signal.SIGTERM
        stop(loader, child_signal)
        stop(monitor, child_signal)
        stop_pid(portal_node_pid, child_signal)
        stop(portal, child_signal)
        stop(observer, child_signal)
    complete_artifacts = all((run_dir / filename).is_file() for filename in ('load.json', 'observer.json', 'monitor.jsonl'))
    if interrupted:
        if complete_artifacts:
            subprocess.check_call([sys.executable, str(STRESS_DIR / 'report.py'), '--run-dir', str(run_dir)])
            print(f'test interrupted; partial results written to {run_dir}', flush=True)
        else:
            print(f'test interrupted; partial artifacts written to {run_dir}', flush=True)
        return
    if not complete_artifacts:
        raise RuntimeError(f'run did not complete; inspect logs in {run_dir}')
    subprocess.check_call([sys.executable, str(STRESS_DIR / 'report.py'), '--run-dir', str(run_dir)])
    print(f'test complete; results written to {run_dir}', flush=True)


if __name__ == '__main__':
    main()
