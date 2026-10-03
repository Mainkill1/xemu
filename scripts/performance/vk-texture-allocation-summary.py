#!/usr/bin/env python3
"""Validate one finalized texture-allocation trace; summarize actual VMA calls.

Durations bracket VMA calls in host elapsed microseconds, not thread CPU time.
Configuration matches after destruction are unbounded opportunities, not pool
hits, predicted speedups or proof of a safe retention/lifetime policy.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re

CONFIG_FIELDS = (
    's_type', 'flags', 'image_type', 'format', 'width', 'height', 'depth',
    'mip_levels', 'array_layers', 'samples', 'tiling', 'usage', 'sharing_mode',
    'initial_layout', 'allocation_flags', 'allocation_usage', 'required_flags',
    'preferred_flags', 'memory_type_bits', 'priority_bits', 'min_alignment',
)
SCOPE = 'ordinary_texture_cache_including_surface_copy_excluding_dummy'
HEX_HANDLE = re.compile(r'[0-9a-f]{16}\Z')


def uint(row, field, bits=64):
    value = row.get(field)
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError(f'Invalid {field}')
    return value


def boolean(row, field):
    value = row.get(field)
    if type(value) is not bool:
        raise ValueError(f'Invalid {field}')
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'Duplicate JSON field {key}')
        result[key] = value
    return result


def read_record(stream):
    line = stream.readline(65537)
    if not line:
        return None
    if len(line) > 65536 or not line.endswith('\n'):
        raise ValueError('Oversized or truncated record')
    record = json.loads(line, object_pairs_hook=unique_object)
    if not isinstance(record, dict):
        raise ValueError('Expected JSON object')
    return record


def read_schema(stream):
    row = read_record(stream)
    if (not row or row.get('type') != 'schema'
            or uint(row, 'schema_version') != 2
            or row.get('duration_unit') != 'host_elapsed_us'
            or row.get('frame_meaning') != 'completed_renderer_flip_stalls'
            or row.get('scope') != SCOPE
            or not 1 <= uint(row, 'max_records') <= 100000):
        raise ValueError('Unsupported allocation trace schema')
    return {'schemaVersion': 2, 'mode': 'liveHeaderOnly',
            'maxRecords': row['max_records'], 'durationMeaning': 'Host elapsed microseconds'}


def handle(row, field):
    value = row.get(field)
    if not isinstance(value, str) or not HEX_HANDLE.fullmatch(value):
        raise ValueError(f'Invalid {field} handle')
    return value


def summarize(stream):
    header = read_schema(stream)
    totals = Counter(create_calls=0, successful_creates=0, destroy_calls=0,
                     create_elapsed_total_us=0, destroy_elapsed_total_us=0,
                     create_elapsed_max_us=0, destroy_elapsed_max_us=0)
    live, live_allocations, retired, classes = {}, set(), Counter(), {}
    records = created_bytes = live_bytes = peak_bytes = null_calls = excluded = 0
    opportunities = opportunity_bytes = 0
    last_frame = last_start = 0
    largest = None
    teardown_started = False
    while True:
        row = read_record(stream)
        if row is None:
            raise ValueError('Missing final lifecycle summary')
        if row.get('type') == 'summary':
            if (not boolean(row, 'complete') or uint(row, 'dropped')
                    or uint(row, 'clock_errors') or uint(row, 'records') != records
                    or any(uint(row, field) != value for field, value in totals.items())):
                raise ValueError('Incomplete or inconsistent lifecycle summary')
            end_reason = row.get('end_reason')
            if end_reason not in ('renderer_teardown', 'shutdown_checkpoint'):
                raise ValueError('Missing or unknown observation end reason')
            if (read_record(stream) is not None
                    or (end_reason == 'renderer_teardown' and live)
                    or (end_reason == 'shutdown_checkpoint' and teardown_started)):
                raise ValueError('Trailing records or inconsistent resource end state')
            break
        kind = row.get('type')
        if kind not in ('create', 'destroy'):
            raise ValueError('Unknown lifecycle event')
        records += 1
        frame, start = uint(row, 'frame'), uint(row, 'start_us', 63)
        duration = uint(row, 'elapsed_us')
        uint(row, 'submission', 32)
        if (uint(row, 'seq') != records or records > header['maxRecords']
                or frame < last_frame or start < last_start):
            raise ValueError('Missing or out-of-order lifecycle event')
        last_frame, last_start = frame, start
        totals[kind + '_calls'] += 1
        totals[kind + '_elapsed_total_us'] += duration
        totals[kind + '_elapsed_max_us'] = max(totals[kind + '_elapsed_max_us'], duration)
        image, allocation = handle(row, 'image'), handle(row, 'allocation')
        if kind == 'create':
            if teardown_started:
                raise ValueError('Creation after renderer teardown')
            result = row.get('result')
            if type(result) is not int or not -(1 << 31) <= result < 1 << 31:
                raise ValueError('Invalid allocation result')
            config = row.get('config')
            if not isinstance(config, dict) or set(config) != set(CONFIG_FIELDS):
                raise ValueError('Incomplete or unsupported configuration fields')
            values = tuple(uint(config, field, 64 if field == 'min_alignment' else 32)
                           for field in CONFIG_FIELDS)
            surface_copy = boolean(row, 'surface_copy')
            supported = boolean(row, 'key_supported')
            pnext = boolean(row, 'pnext_present')
            families = uint(row, 'queue_family_count', 32)
            custom_pool = boolean(row, 'custom_pool')
            user_data = boolean(row, 'user_data')
            expected_supported = not (pnext or families or custom_pool or user_data
                                      or config['s_type'] != 14)
            if supported != expected_supported:
                raise ValueError('Inconsistent configuration support marker')
            size, memory_type = uint(row, 'size_bytes'), uint(row, 'memory_type', 32)
            if result != 0:
                if int(image, 16) or int(allocation, 16) or size or memory_type:
                    raise ValueError('Failure contains successful allocation metadata')
                continue
            if (not int(image, 16) or not int(allocation, 16) or not size
                    or image in live or allocation in live_allocations):
                raise ValueError('Invalid or overlapping successful allocation')
            totals['successful_creates'] += 1
            created_bytes += size
            live_bytes += size
            peak_bytes = max(peak_bytes, live_bytes)
            key = values + (memory_type, size)
            eligible = supported and not surface_copy
            live[image] = (allocation, size, key, eligible)
            live_allocations.add(allocation)
            if eligible:
                info = classes.setdefault(key, {'config': config, 'memoryType': memory_type,
                                                'sizeBytes': size, 'creates': 0,
                                                'unboundedReuseOpportunities': 0})
                info['creates'] += 1
                if retired[key]:
                    retired[key] -= 1
                    opportunities += 1
                    opportunity_bytes += size
                    info['unboundedReuseOpportunities'] += 1
            else:
                excluded += 1
            if largest is None or duration > largest['elapsed_us']:
                largest = row
        else:
            teardown = boolean(row, 'teardown')
            if teardown_started and not teardown:
                raise ValueError('Destruction leaves teardown phase')
            teardown_started |= teardown
            if not int(image, 16) and not int(allocation, 16):
                null_calls += 1
                continue
            item = live.pop(image, None)
            if item is None or item[0] != allocation:
                raise ValueError('Unmatched or stale destruction')
            live_allocations.remove(allocation)
            live_bytes -= item[1]
            if item[3] and not teardown:
                retired[item[2]] += 1
    return {
        'schemaVersion': 2, 'mode': 'finalizedFullLog', 'records': records,
        'endReason': end_reason,
        'lifecycleCoverage': ('Complete observation window through idle shutdown checkpoint; '
                              'live images are not destruction events; later cleanup unobserved.'
                              if end_reason == 'shutdown_checkpoint' else
                              'Complete renderer lifetime through resource teardown.'),
        'durationMeaning': 'Host elapsed milliseconds bracketing VMA calls; not CPU time. '
                           'Metadata/output cost and whole probe overhead are not measured.',
        'creates': {'calls': totals['create_calls'], 'successful': totals['successful_creates'],
                    'elapsedTotalMs': totals['create_elapsed_total_us'] / 1000,
                    'elapsedMaxMs': totals['create_elapsed_max_us'] / 1000},
        'destroys': {'calls': totals['destroy_calls'], 'nullCalls': null_calls,
                     'elapsedTotalMs': totals['destroy_elapsed_total_us'] / 1000,
                     'elapsedMaxMs': totals['destroy_elapsed_max_us'] / 1000},
        'totalCreatedBytes': created_bytes, 'peakLiveBytes': peak_bytes,
        'liveImagesAtEnd': len(live), 'liveBytesAtEnd': live_bytes,
        'configurationCount': len(classes),
        'excludedSuccessfulCreates': excluded,
        'unboundedReuseOpportunities': opportunities, 'unboundedReuseBytes': opportunity_bytes,
        'reuseMeaning': 'Compatible ordinary image creation after destruction, ignoring retention '
                        'limits and memory pressure; not a bounded pool hit rate or speedup estimate. '
                        'Surface-copy and unsupported configurations excluded; safety unproven.',
        'configurations': sorted(classes.values(), key=lambda item: -item['creates']),
        'largestSuccessfulCreate': largest,
    }


def file_identity(stat):
    return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns


def sha256_stream(stream):
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b''):
        digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--schema-only', action='store_true')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    try:
        with args.source.open(encoding='utf-8') as stream:
            before = os.fstat(stream.fileno())
            result = read_schema(stream) if args.schema_only else summarize(stream)
            result['sourcePath'] = str(args.source)
            if args.schema_only:
                result['sourceBytesAtRead'] = before.st_size
            else:
                stream.seek(0)
                result['sourceSha256'] = sha256_stream(stream.buffer)
                if (file_identity(before) != file_identity(os.fstat(stream.fileno()))
                        or file_identity(before) != file_identity(args.source.stat())):
                    raise ValueError('Source changed during analysis')
                result['sourceBytes'] = before.st_size
        text = json.dumps(result, indent=2) + '\n'
        if args.out:
            with args.out.open('x', encoding='utf-8') as output:
                output.write(text)
        else:
            print(text, end='')
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
