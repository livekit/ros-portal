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

import importlib.util
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / 'scripts' / 'analyze_latency_trace.py'
SPEC = importlib.util.spec_from_file_location('analyze_latency_trace', SCRIPT)
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


def event(name, event_time, **fields):
    return {'_name': name, '_timestamp': event_time, **fields}


def test_build_latency_report_correlates_bridge_and_ros_events():
    correlation_id = 42
    source_timestamp = 1001
    inbound_source_timestamp = 2002
    topic = '/ros_portal/latency/probe'
    events = [
        event('ros2:rmw_publish', 1_000_000, timestamp=source_timestamp, vpid=10, vtid=11),
        event(
            'ros_portal:outbound_received', 2_000_000,
            correlation_id=correlation_id, source_timestamp=source_timestamp,
            topic=topic, payload_size=64, vpid=20, vtid=21),
        event(
            'ros_portal:livekit_push', 3_000_000,
            correlation_id=correlation_id, topic=topic, payload_size=64),
        event(
            'ros_portal:livekit_received', 8_000_000,
            correlation_id=correlation_id, track=topic, payload_size=64),
        event(
            'ros_portal:ros_publish', 9_000_000,
            correlation_id=correlation_id, topic=topic, payload_size=64,
            vpid=30, vtid=31),
        event(
            'ros2:rmw_publish', 9_100_000, timestamp=inbound_source_timestamp,
            vpid=30, vtid=31),
        event(
            'ros2:rmw_take', 10_000_000, source_timestamp=inbound_source_timestamp,
            taken=1, vpid=40, vtid=41),
    ]

    report = ANALYZER.build_latency_report(events, topic=topic, warmup=0)

    assert report['complete_records'] == 1
    sample = report['samples'][0]
    assert sample['t0_t1'] == 1.0
    assert sample['t1_t2'] == 1.0
    assert sample['t2_t3'] == 5.0
    assert sample['t3_t4'] == 1.0
    assert sample['t4_t5'] == 1.0
    assert sample['bridge_internal'] == 2.0
    assert sample['e2e'] == 9.0


def test_build_latency_report_drops_warmup_and_ignores_zero_correlation():
    topic = '/ros_portal/latency/probe'
    events = []
    for index, correlation_id in enumerate((0, 1, 2, 3)):
        base = index * 10_000_000
        events.extend([
            event(
                'ros_portal:outbound_received', base + 1_000_000,
                correlation_id=correlation_id, source_timestamp=index, topic=topic,
                payload_size=8),
            event(
                'ros_portal:livekit_push', base + 2_000_000,
                correlation_id=correlation_id, topic=topic, payload_size=8),
            event(
                'ros_portal:livekit_received', base + 3_000_000,
                correlation_id=correlation_id, track=topic, payload_size=8),
            event(
                'ros_portal:ros_publish', base + 4_000_000,
                correlation_id=correlation_id, topic=topic, payload_size=8),
        ])

    report = ANALYZER.build_latency_report(events, topic=topic, warmup=1)

    assert report['records'] == 3
    assert report['complete_records'] == 3
    assert report['warmup_discarded'] == 1
    assert len(report['samples']) == 2
    assert report['metrics']['bridge_internal']['count'] == 2
    assert report['metrics']['e2e']['count'] == 0
