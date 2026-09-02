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

"""Summarize ros_portal latency tracepoints from a CTF trace."""

import argparse
import csv
import json
import math
from pathlib import Path
import sys


CUSTOM_PREFIX = 'ros_portal:'
CUSTOM_EVENTS = {
    f'{CUSTOM_PREFIX}outbound_received': 't1',
    f'{CUSTOM_PREFIX}livekit_push': 't2',
    f'{CUSTOM_PREFIX}livekit_received': 't3',
    f'{CUSTOM_PREFIX}ros_publish': 't4',
}
SEGMENTS = ('e2e', 'bridge_internal', 't0_t1', 't1_t2', 't2_t3', 't3_t4', 't4_t5')


def _field(event, name, default=None):
    """Read a Babeltrace field while tolerating context-field naming variants."""
    for candidate in (name, f'_{name}'):
        if candidate in event:
            return event[candidate]
    return default


def _nearest_rank(sorted_values, percentile):
    rank = max(1, math.ceil(percentile / 100.0 * len(sorted_values)))
    return sorted_values[min(rank, len(sorted_values)) - 1]


def _metric(values):
    ordered = sorted(values)
    if not ordered:
        return {'count': 0}
    return {
        'count': len(ordered),
        'p50_ms': _nearest_rank(ordered, 50),
        'p90_ms': _nearest_rank(ordered, 90),
        'p95_ms': _nearest_rank(ordered, 95),
        'p99_ms': _nearest_rank(ordered, 99),
        'min_ms': ordered[0],
        'max_ms': ordered[-1],
        'mean_ms': sum(ordered) / len(ordered),
    }


def _matching_publish_before(publishes_by_source, source_timestamp, before):
    candidates = publishes_by_source.get(source_timestamp, ())
    matching = [event for event in candidates if event['_timestamp'] <= before]
    return max(matching, key=lambda event: event['_timestamp']) if matching else None


def _matching_publish_after(publishes_by_thread, record):
    thread = (record.get('t4_vpid'), record.get('t4_vtid'))
    candidates = publishes_by_thread.get(thread, ())
    for event in candidates:
        if event['_timestamp'] >= record['t4'] and not event.get('_latency_claimed'):
            event['_latency_claimed'] = True
            return event
    return None


def _matching_take_after(takes_by_source, source_timestamp, after):
    candidates = takes_by_source.get(source_timestamp, ())
    matching = [
        event for event in candidates
        if event['_timestamp'] >= after and int(event.get('taken', 1)) != 0
    ]
    return min(matching, key=lambda event: event['_timestamp']) if matching else None


def build_latency_report(events, topic=None, warmup=20):
    """Build per-frame samples and aggregate metrics from trace event dictionaries."""
    ordered_events = sorted(
        (dict(event) for event in events),
        key=lambda event: event['_timestamp'],
    )
    records = {}
    publishes_by_source = {}
    publishes_by_thread = {}
    takes_by_source = {}

    for event in ordered_events:
        name = event.get('_name')
        if name == 'ros2:rmw_publish':
            source_timestamp = int(event.get('timestamp', 0))
            publishes_by_source.setdefault(source_timestamp, []).append(event)
            thread = (_field(event, 'vpid'), _field(event, 'vtid'))
            publishes_by_thread.setdefault(thread, []).append(event)
            continue
        if name == 'ros2:rmw_take':
            source_timestamp = int(event.get('source_timestamp', 0))
            takes_by_source.setdefault(source_timestamp, []).append(event)
            continue
        stamp_name = CUSTOM_EVENTS.get(name)
        if stamp_name is None:
            continue
        if topic is not None and event.get('topic', event.get('track')) != topic:
            continue
        correlation_id = int(event.get('correlation_id', 0))
        if correlation_id == 0:
            continue
        record = records.setdefault(correlation_id, {'correlation_id': correlation_id})
        record[stamp_name] = event['_timestamp']
        if stamp_name == 't1':
            record['source_timestamp'] = int(event.get('source_timestamp', 0))
            record['topic'] = event.get('topic', '')
            record['payload_size'] = int(event.get('payload_size', 0))
        elif stamp_name == 't4':
            record['t4_vpid'] = _field(event, 'vpid')
            record['t4_vtid'] = _field(event, 'vtid')

    complete_records = [
        record for record in records.values()
        if all(stamp in record for stamp in ('t1', 't2', 't3', 't4'))
    ]
    complete_records.sort(key=lambda record: record['t1'])
    measured_records = complete_records[warmup:]

    samples = []
    for record in measured_records:
        t0_event = _matching_publish_before(
            publishes_by_source, record.get('source_timestamp', 0), record['t1'])
        t4_publish = _matching_publish_after(publishes_by_thread, record)
        t5_event = None
        if t4_publish is not None:
            t5_event = _matching_take_after(
                takes_by_source, int(t4_publish.get('timestamp', 0)), t4_publish['_timestamp'])

        stamps = {stamp: record[stamp] for stamp in ('t1', 't2', 't3', 't4')}
        if t0_event is not None:
            stamps['t0'] = t0_event['_timestamp']
        if t5_event is not None:
            stamps['t5'] = t5_event['_timestamp']

        sample = {
            'correlation_id': record['correlation_id'],
            'topic': record.get('topic', ''),
            'payload_size': record.get('payload_size', 0),
            't1_t2': (stamps['t2'] - stamps['t1']) / 1e6,
            't2_t3': (stamps['t3'] - stamps['t2']) / 1e6,
            't3_t4': (stamps['t4'] - stamps['t3']) / 1e6,
        }
        sample['bridge_internal'] = sample['t1_t2'] + sample['t3_t4']
        if 't0' in stamps:
            sample['t0_t1'] = (stamps['t1'] - stamps['t0']) / 1e6
        if 't5' in stamps:
            sample['t4_t5'] = (stamps['t5'] - stamps['t4']) / 1e6
        if 't0' in stamps and 't5' in stamps:
            sample['e2e'] = (stamps['t5'] - stamps['t0']) / 1e6
        samples.append(sample)

    metrics = {
        segment: _metric([sample[segment] for sample in samples if segment in sample])
        for segment in SEGMENTS
    }
    return {
        'records': len(records),
        'complete_records': len(complete_records),
        'warmup_discarded': min(warmup, len(complete_records)),
        'samples': samples,
        'metrics': metrics,
    }


def load_trace_events(trace_directory):
    """Load all CTF events using the ROS tracetools reader."""
    try:
        from tracetools_read.trace import get_trace_events
    except ImportError as error:
        raise RuntimeError(
            'tracetools_read and its Babeltrace Python dependency are required') from error
    return get_trace_events(str(trace_directory))


def _write_csv(path, samples):
    columns = ('correlation_id', 'topic', 'payload_size', *SEGMENTS)
    with path.open('w', newline='', encoding='utf-8') as output:
        writer = csv.DictWriter(output, fieldnames=columns, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(samples)


def _print_report(report):
    print(
        'trace records={} complete={} warmup={} measured={}'.format(
            report['records'], report['complete_records'],
            report['warmup_discarded'], len(report['samples'])))
    for name in SEGMENTS:
        metric = report['metrics'][name]
        if metric['count'] == 0:
            print(f'{name:16} no correlated samples')
            continue
        print(
            '{:16} n={:<6} p50={:9.3f} ms  p95={:9.3f} ms  '
            'p99={:9.3f} ms  max={:9.3f} ms'.format(
                name, metric['count'], metric['p50_ms'], metric['p95_ms'],
                metric['p99_ms'], metric['max_ms']))


def _check_budget(report, minimum_samples, p50_budget, p95_budget):
    metric = report['metrics']['bridge_internal']
    failures = []
    if metric['count'] < minimum_samples:
        failures.append(
            f"bridge_internal has {metric['count']} samples; expected at least {minimum_samples}")
    if metric['count'] and p50_budget is not None and metric['p50_ms'] > p50_budget:
        failures.append(
            f"bridge_internal p50 {metric['p50_ms']:.3f} ms exceeds {p50_budget:.3f} ms")
    if metric['count'] and p95_budget is not None and metric['p95_ms'] > p95_budget:
        failures.append(
            f"bridge_internal p95 {metric['p95_ms']:.3f} ms exceeds {p95_budget:.3f} ms")
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace_directory', type=Path)
    parser.add_argument('--topic', default='/ros_portal/latency/probe')
    parser.add_argument('--warmup', type=int, default=20)
    parser.add_argument('--json', dest='json_path', type=Path)
    parser.add_argument('--csv', dest='csv_path', type=Path)
    parser.add_argument('--minimum-samples', type=int, default=1)
    parser.add_argument('--max-bridge-p50-ms', type=float)
    parser.add_argument('--max-bridge-p95-ms', type=float)
    args = parser.parse_args(argv)

    if args.warmup < 0:
        parser.error('--warmup must be non-negative')
    if args.minimum_samples < 0:
        parser.error('--minimum-samples must be non-negative')

    try:
        events = load_trace_events(args.trace_directory)
        report = build_latency_report(events, topic=args.topic, warmup=args.warmup)
    except (OSError, RuntimeError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 2

    _print_report(report)
    if args.json_path is not None:
        args.json_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if args.csv_path is not None:
        _write_csv(args.csv_path, report['samples'])

    failures = _check_budget(
        report, args.minimum_samples, args.max_bridge_p50_ms, args.max_bridge_p95_ms)
    for failure in failures:
        print(f'error: {failure}', file=sys.stderr)
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
