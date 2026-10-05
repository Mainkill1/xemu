#!/usr/bin/env python3
"""Read existing Vulkan schema-8 telemetry without summing nested durations.

Adapted from the maintained reader in xemu PR #293 at d1970118f30; --finish
adds sampled finish attribution for #250 without changing the emulator.
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

MAX_LINE_BYTES = 65536
UINT64_MAX = (1 << 64) - 1


class HashedUtf8Lines:
    """Decode one bounded record and hash the exact bytes consumed."""
    def __init__(self, raw):
        self.raw = raw
        self.digest = hashlib.sha256()
        self.bytes_read = 0

    def readline(self, limit):
        data = self.raw.readline(limit)
        if len(data) > MAX_LINE_BYTES:
            raise ValueError('Oversized telemetry record')
        self.digest.update(data)
        self.bytes_read += len(data)
        return data.decode('utf-8')


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f'Duplicate telemetry key: {key}')
        value[key] = item
    return value


def invalid_constant(value):
    raise ValueError(f'Non-JSON telemetry constant: {value}')


def read_record(stream):
    line = stream.readline(MAX_LINE_BYTES + 1)
    if not line:
        return None
    if len(line) > MAX_LINE_BYTES or not line.endswith('\n'):
        raise ValueError('Oversized or incomplete telemetry record')
    value = json.loads(line, object_pairs_hook=unique_object,
                       parse_constant=invalid_constant)
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
    if type(value) is not int or not 0 <= value <= UINT64_MAX:
        raise ValueError('Expected a uint64 integer counter')
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
        if (row.get('type') != 'frame' or
                type(row.get('schema_version')) is not int or
                row['schema_version'] != 8):
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
            duration = nonnegative_integer(duration)
            occurrence = nonnegative_integer(occurrence)
            if duration and not occurrence:
                raise ValueError('CPU elapsed time without a call')
            totals[i] += duration
            calls[i] += occurrence
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


FINISH_REASONS = {
    'vertex_buffer_dirty', 'surface_create', 'surface_down', 'need_buffer_space',
    'framebuffer_dirty', 'presenting', 'flip_stall', 'flush', 'stalled',
    'texture_dirty',
}
FINISH_FIELDS = {
    'calls': 'finish_count_per_guest_frame',
    'submits': 'finish_submit_count_per_guest_frame',
    'timingSamples': 'finish_timed_submit_count_per_guest_frame',
    'fenceWaits': 'fence_wait_count_per_guest_frame',
    'sampledSubmitHostElapsedMs': 'finish_sampled_submit_cpu_us_per_guest_frame',
    'sampledFenceWaitHostElapsedMs': 'finish_sampled_wait_us_per_guest_frame',
}


def finish_totals(names, buckets):
    totals = {name: {key: 0 for key in FINISH_FIELDS} for name in names}
    for bucket in buckets:
        for name, stats in bucket['reasons'].items():
            for key, value in stats.items():
                totals[name][key] += value
    for stats in totals.values():
        for key in ('sampledSubmitHostElapsedMs', 'sampledFenceWaitHostElapsedMs'):
            stats[key] /= 1000
        stats['durationCoverage'] = (
            'no_submissions' if stats['submits'] == 0 else
            'all_submissions' if stats['timingSamples'] == stats['submits'] else
            'partial')
    return totals


def summarize_finish(stream, tail_seconds=None):
    """Attribute exact call counts and sampled times, without extrapolation.

    Sampling follows finish-call ordinal, including calls that do not submit.
    Tail storage follows the auxiliary reader's time-window contract; a bucket
    crossing the boundary cannot place its individual events within the tail.
    """
    if tail_seconds is not None and (type(tail_seconds) is not int or tail_seconds <= 0):
        raise ValueError('Tail seconds must be a positive integer')
    schema = read_record(stream)
    if (not schema or schema.get('type') != 'schema' or
            type(schema.get('schema_version')) is not int or schema['schema_version'] != 8):
        raise ValueError('Expected Vulkan telemetry schema 8')
    names = schema.get('finish_reasons')
    if (not isinstance(names, list) or len(names) != 10 or
            any(not isinstance(name, str) for name in names) or set(names) != FINISH_REASONS):
        raise ValueError('Expected the ten schema-8 finish reasons')
    sampling = schema.get('duration_sampling')
    if (not isinstance(sampling, dict) or
            type(sampling.get('initial_per_reason_per_frame')) is not int or
            type(sampling.get('hot_stride')) is not int or
            sampling != {'initial_per_reason_per_frame': 8, 'hot_stride': 16}):
        raise ValueError('Unsupported schema-8 finish sampling policy')
    totals = {name: {key: 0 for key in FINISH_FIELDS} for name in names}
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
        for key, field in FINISH_FIELDS.items():
            values = row.get(field)
            if not isinstance(values, list) or len(values) != len(names):
                raise ValueError(f'Missing or mismatched {field} array')
            for name, value in zip(names, values):
                counters[name][key] = nonnegative_integer(value)
        for name, stats in counters.items():
            if not 0 <= stats['timingSamples'] <= stats['submits'] <= stats['calls']:
                raise ValueError('Finish samples/submits/calls contradict each other')
            scheduled = min(stats['calls'], 8) + stats['calls'] // 16
            # At most the non-submitting calls can consume scheduled samples.
            minimum_samples = max(0, stats['submits'] -
                                  (stats['calls'] - scheduled))
            if stats['timingSamples'] < minimum_samples:
                raise ValueError('Finish sampling omits required submissions')
            if stats['fenceWaits'] != stats['submits']:
                raise ValueError('Schema-8 finish submissions must each wait on the fence')
            if not stats['timingSamples'] and (stats['sampledSubmitHostElapsedMs'] or stats['sampledFenceWaitHostElapsedMs']):
                raise ValueError('Finish elapsed time without a timing sample')
            for key, value in stats.items():
                totals[name][key] += value
        if tail_seconds is not None:
            tail.append({'guestFrame': frame, 'startTimestampUs': previous_timestamp,
                         'endTimestampUs': timestamp, 'reasons': counters})
            while tail and tail[0]['endTimestampUs'] <= timestamp - tail_seconds * 1000000:
                tail.popleft()
        if first_timestamp is None:
            first_timestamp = timestamp
        count += 1
        previous_timestamp = timestamp
    if not count:
        raise ValueError('No telemetry frames')
    result = {
        'schemaVersion': 8, 'mode': 'finalizedFinishLog', 'frameRecords': count,
        'observedTimestampSpanMs': (previous_timestamp - first_timestamp) / 1000,
        'durationSampling': sampling,
        'durationMeaning': 'Host elapsed sampled submit/fence-wait milliseconds, not CPU time, GPU work or predicted savings. Durations are not extrapolated; partial coverage omits untimed submissions.',
        'coverageMeaning': 'Counters are grouped at PGRAPH control-frame boundaries; first bucket start and activity after the final record are unobserved. Not displayed FPS.',
        'all': {'reasons': finish_totals(names, [{'reasons': totals}])},
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
            boundary = {key: value for key, value in overlap[0].items() if key != 'reasons'}
            boundary['reasons'] = finish_totals(names, overlap)
        result['tail'] = {'seconds': tail_seconds, 'startTimestampUs': start,
                          'endTimestampUs': previous_timestamp,
                          'completeBucketCount': len(complete),
                          'completeBuckets': finish_totals(names, complete),
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
    parser.add_argument('--finish', action='store_true',
                        help='Summarize finish reason counts and sampled fence waits')
    parser.add_argument('--tail-seconds', type=int,
                        help='Finish/auxiliary trailing window; straddling bucket is separate')
    args = parser.parse_args()
    if sum((args.auxiliary, args.finish, args.schema_only)) > 1 or (args.tail_seconds is not None and not (args.auxiliary or args.finish)):
        parser.error('Select one analysis mode; tail seconds requires --auxiliary or --finish')
    with args.log.open('rb') as raw:
        before = os.fstat(raw.fileno())
        stream = HashedUtf8Lines(raw)
        if args.schema_only:
            result = {'schemaVersion': 8, 'mode': 'liveHeaderOnly',
                      'regions': read_schema(stream),
                      'sourceBytesAtRead': before.st_size}
        elif args.auxiliary:
            result = summarize_auxiliary(stream, args.tail_seconds)
        elif args.finish:
            result = summarize_finish(stream, args.tail_seconds)
        else:
            result = summarize(stream)
        result['sourcePath'] = str(args.log)
        if not args.schema_only:
            result['sourceSha256'] = stream.digest.hexdigest()
            identity = lambda stat: (stat.st_dev, stat.st_ino, stat.st_size,
                                     stat.st_mtime_ns)
            if (identity(before) != identity(os.fstat(raw.fileno())) or
                    identity(before) != identity(args.log.stat()) or
                    stream.bytes_read != before.st_size):
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
