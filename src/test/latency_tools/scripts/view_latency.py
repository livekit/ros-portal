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

import argparse
import json

import matplotlib.pyplot as plt


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Plot latency metrics from a JSON report file.')
    parser.add_argument('json_file', help='Path to the latency JSON report')
    args = parser.parse_args(argv)

    with open(args.json_file, encoding='utf-8') as file:
        report = json.load(file)

    segments = [
        'e2e',
        'bridge_internal',
        't0_t1',
        't1_t2',
        't2_t3',
        't3_t4',
        't4_t5',
    ]

    labels = [
        'End to end',
        'Bridge internal',
        'ROS publish → bridge',
        'Bridge → LiveKit',
        'LiveKit transport',
        'LiveKit → ROS publish',
        'ROS publish → take',
    ]

    metrics = report['metrics']
    p50 = [metrics[name]['p50_ms'] for name in segments]
    p95 = [metrics[name]['p95_ms'] for name in segments]

    figure, axis = plt.subplots(figsize=(11, 6))
    positions = range(len(segments))

    axis.barh([position - 0.2 for position in positions], p50, 0.4, label='p50')
    axis.barh([position + 0.2 for position in positions], p95, 0.4, label='p95')
    axis.set_yticks(list(positions), labels)
    axis.invert_yaxis()
    axis.set_xlabel('Latency (ms)')
    axis.set_title('ROS 2 ↔ LiveKit Latency')
    axis.legend()
    axis.grid(axis='x', alpha=0.25)

    figure.tight_layout()
    figure.savefig('latency-results.png', dpi=180)


if __name__ == '__main__':
    main()
