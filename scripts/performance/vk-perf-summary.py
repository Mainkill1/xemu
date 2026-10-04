#!/usr/bin/env python3
"""Read existing Vulkan schema-8 telemetry without summing nested durations.

The full summary requires a finalized log. --schema-only validates just the
first record of a live log for a small runner evidence artifact. Neither mode
measures allocation time or modifies emulator state.
"""
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import sys

MAX_LINE_CHARS = 65536


def sha256_stream(stream):
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b''):
        digest.update(chunk)
    return digest.hexdigest()


def read_record(stream):
    line = stream.readline(MAX_LINE_CHARS + 1)
    if not line:
        return None
    if len(line) > MAX_LINE_CHARS or not line.endswith('\n'):
        raise ValueError('Oversized or incomplete telemetry record')
    value = json.loads(line)
    if not isinstance(value, dict):
        raise ValueError('Telemetry record must be an object')
    return value


def read_schema(stream, field='cpu_regions'):
    value = read_record(stream)
    if (not value or value.get('type') != 'schema' or
            type(value.get('schema_version')) is not int or
            value['schema_version'] != 8):
        raise ValueError('Expected Vulkan telemetry schema 8')
    names = value.get(field)
    if (not isinstance(names, list) or not names or
            any(not isinstance(x, str) or not x for x in names) or
            len(set(names)) != len(names)):
        raise ValueError(f'Expected unique {field} names')
    return names


def nonnegative_integer(value):
    if type(value) is not int or value < 0:
        raise ValueError('Expected a nonnegative integer counter')
    return value


def summarize(stream):
    names = read_schema(stream)
    totals = [0] * len(names)
    calls = [0] * len(names)
    maxima = [-1] * len(names)
    maximum_frames = [None] * len(names)
    count = 0
    first_timestamp = None
    previous_timestamp = -1
    previous_frame = 0
    while (row := read_record(stream)) is not None:
        if row.get('type') != 'frame' or row.get('schema_version') != 8:
            raise ValueError('Expected a schema-8 frame record')
        frame = nonnegative_integer(row.get('guest_frame'))
        timestamp = nonnegative_integer(row.get('timestamp_us'))
        if frame != previous_frame + 1 or timestamp < previous_timestamp:
            raise ValueError('Missing/out-of-order frame or host clock regression')
        elapsed = row.get('cpu_region_us_per_guest_frame')
        occurrences = row.get('cpu_region_calls_per_guest_frame')
        if (not isinstance(elapsed, list) or not isinstance(occurrences, list) or
                len(elapsed) != len(names) or len(occurrences) != len(names)):
            raise ValueError('Missing or mismatched CPU region arrays')
        for i, (duration, occurrence) in enumerate(zip(elapsed, occurrences)):
            totals[i] += nonnegative_integer(duration)
            calls[i] += nonnegative_integer(occurrence)
            if duration > maxima[i]:
                maxima[i] = duration
                maximum_frames[i] = frame
        if first_timestamp is None:
            first_timestamp = timestamp
        count += 1
        previous_timestamp = timestamp
        previous_frame = frame
    if not count:
        raise ValueError('No telemetry frames')
    return {
        'schemaVersion': 8, 'mode': 'finalizedFullLog', 'frameRecords': count,
        'observedTimestampSpanMs': (previous_timestamp - first_timestamp) / 1000,
        'durationMeaning': 'Host elapsed milliseconds; regions are nested. '
                           'Do not sum them or interpret them as allocation cost.',
        'regions': {name: {'calls': calls[i], 'elapsedTotalMs': totals[i] / 1000,
                           'maximumFrameElapsedMs': maxima[i] / 1000,
                           'maximumFrame': maximum_frames[i]}
                    for i, name in enumerate(names)},
    }


def write_report(path, report):
    with Path(path).open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')


AUXILIARY_CALLERS = {
    'pvideo_upload', 'display_render', 'surface_download', 'surface_create',
    'surface_upload', 'texture_upload', 'dummy_texture_create',
}
AUXILIARY_FIELDS = {
    'submits': 'single_time_submit_count_per_guest_frame',
    'timedSubmits': 'single_time_timed_submit_count_per_guest_frame',
    'queueIdleWaits': 'queue_wait_idle_count_per_guest_frame',
    'submitHostElapsedMs': 'single_time_sampled_submit_cpu_us_per_guest_frame',
    'queueIdleWaitHostElapsedMs': 'single_time_sampled_wait_us_per_guest_frame',
}


def auxiliary_totals(names, buckets):
    totals = {name: {key: 0 for key in AUXILIARY_FIELDS} for name in names}
    for bucket in buckets:
        for name in names:
            for key, value in bucket['callers'][name].items():
                totals[name][key] += value
    for stats in totals.values():
        for key in ('submitHostElapsedMs', 'queueIdleWaitHostElapsedMs'):
            stats[key] /= 1000
    return totals


def summarize_auxiliary(stream, tail_seconds=None):
    """Read exact schema-8 auxiliary counters; bound tail storage by time.

    Frame counters cover intervals between records. A bucket crossing the
    requested start cannot establish which side its individual events used.
    Report it separately, and exclude it from complete-window totals.
    """
    if tail_seconds is not None and (type(tail_seconds) is not int or tail_seconds <= 0):
        raise ValueError('Tail seconds must be a positive integer')
    names = read_schema(stream, 'single_time_callers')
    if set(names) != AUXILIARY_CALLERS:
        raise ValueError('Expected the seven schema-8 auxiliary callers')
    totals = {name: {key: 0 for key in AUXILIARY_FIELDS} for name in names}
    tail = deque()
    count, first_timestamp, previous_timestamp = 0, None, None
    while (row := read_record(stream)) is not None:
        if row.get('type') != 'frame' or type(row.get('schema_version')) is not int or row['schema_version'] != 8:
            raise ValueError('Expected a schema-8 frame record')
        frame = nonnegative_integer(row.get('guest_frame'))
        timestamp = nonnegative_integer(row.get('timestamp_us'))
        if frame != count + 1 or (previous_timestamp is not None and timestamp < previous_timestamp):
            raise ValueError('Missing/out-of-order frame or host clock regression')
        counters = {name: {} for name in names}
        for key, field in AUXILIARY_FIELDS.items():
            values = row.get(field)
            if not isinstance(values, list) or len(values) != len(names):
                raise ValueError(f'Missing or mismatched {field} array')
            for name, value in zip(names, values):
                counters[name][key] = nonnegative_integer(value)
        for name, stats in counters.items():
            if stats['submits'] != stats['timedSubmits'] or stats['submits'] != stats['queueIdleWaits']:
                raise ValueError('Schema-8 auxiliary submits must each be timed and waited')
            if not stats['submits'] and (stats['submitHostElapsedMs'] or stats['queueIdleWaitHostElapsedMs']):
                raise ValueError('Auxiliary elapsed time without a submit')
            for key, value in stats.items():
                totals[name][key] += value
        if tail_seconds is not None:
            tail.append({'guestFrame': frame, 'startTimestampUs': previous_timestamp,
                         'endTimestampUs': timestamp, 'callers': counters})
            while tail and tail[0]['endTimestampUs'] <= timestamp - tail_seconds * 1000000:
                tail.popleft()
        if first_timestamp is None:
            first_timestamp = timestamp
        count += 1
        previous_timestamp = timestamp
    if not count:
        raise ValueError('No telemetry frames')
    for stats in totals.values():
        for key in ('submitHostElapsedMs', 'queueIdleWaitHostElapsedMs'):
            stats[key] /= 1000
    result = {
        'schemaVersion': 8, 'mode': 'finalizedAuxiliaryLog', 'frameRecords': count,
        'observedTimestampSpanMs': (previous_timestamp - first_timestamp) / 1000,
        'durationMeaning': 'Host elapsed submit/wait milliseconds, not CPU time, GPU work, or predicted savings. No nested-region totals are added.',
        'coverageMeaning': 'Counters are grouped at PGRAPH control-frame boundaries; first bucket start and activity after the final record are unobserved. Not displayed FPS.',
        'all': {'callers': totals},
    }
    if tail_seconds is not None:
        start = previous_timestamp - tail_seconds * 1000000
        if first_timestamp > start:
            raise ValueError('Insufficient timestamp coverage for requested tail')
        complete = [bucket for bucket in tail if bucket['startTimestampUs'] >= start]
        overlap = [bucket for bucket in tail if bucket['startTimestampUs'] < start]
        assert len(overlap) <= 1
        boundary = None
        if overlap:
            boundary = {key: value for key, value in overlap[0].items() if key != 'callers'}
            boundary['callers'] = auxiliary_totals(names, overlap)
        result['tail'] = {'seconds': tail_seconds, 'startTimestampUs': start,
                          'endTimestampUs': previous_timestamp,
                          'completeBucketCount': len(complete),
                          'completeBuckets': auxiliary_totals(names, complete),
                          'straddlingBucket': boundary}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--out', type=Path, help='Create a new JSON file; never overwrite')
    parser.add_argument('--schema-only', action='store_true',
                        help='Validate only the header of a live, unfinished log')
    parser.add_argument('--auxiliary', action='store_true',
                        help='Summarize existing auxiliary submit/wait caller arrays')
    parser.add_argument('--tail-seconds', type=int,
                        help='Auxiliary trailing window; straddling bucket is separate')
    args = parser.parse_args()
    if (args.auxiliary and args.schema_only) or (args.tail_seconds is not None and not args.auxiliary):
        parser.error('Auxiliary analysis requires a finalized log; tail seconds requires --auxiliary')
    with args.log.open(encoding='utf-8') as stream:
        before = os.fstat(stream.fileno())
        if args.schema_only:
            result = {'schemaVersion': 8, 'mode': 'liveHeaderOnly',
                      'regions': read_schema(stream),
                      'sourceBytesAtRead': before.st_size}
        elif args.auxiliary:
            result = summarize_auxiliary(stream, args.tail_seconds)
        else:
            result = summarize(stream)
        result['sourcePath'] = str(args.log)
        if not args.schema_only:
            stream.seek(0)
            result['sourceSha256'] = sha256_stream(stream.buffer)
            identity = lambda stat: (stat.st_dev, stat.st_ino, stat.st_size,
                                     stat.st_mtime_ns)
            if (identity(before) != identity(os.fstat(stream.fileno())) or
                    identity(before) != identity(args.log.stat())):
                raise ValueError('Log changed while summarizing; use a finalized log')
            result['sourceBytes'] = before.st_size
    if args.out:
        write_report(args.out, result)
    else:
        json.dump(result, sys.stdout, indent=2, allow_nan=False)
        print()


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as error:
        print(f'vk-perf-summary: {error}', file=sys.stderr)
        sys.exit(1)
