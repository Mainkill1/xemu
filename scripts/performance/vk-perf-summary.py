#!/usr/bin/env python3
"""Read existing Vulkan schema-8 telemetry without summing nested durations.

The full summary requires a finalized log. --schema-only validates just the
first record of a live log for a small runner evidence artifact. Neither mode
measures allocation time or modifies emulator state.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

MAX_LINE_CHARS = 65536


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


def read_schema(stream):
    value = read_record(stream)
    if (not value or value.get('type') != 'schema' or
            type(value.get('schema_version')) is not int or
            value['schema_version'] != 8):
        raise ValueError('Expected Vulkan telemetry schema 8')
    names = value.get('cpu_regions')
    if (not isinstance(names, list) or not names or
            any(not isinstance(x, str) or not x for x in names) or
            len(set(names)) != len(names)):
        raise ValueError('Expected unique CPU region names')
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--out', type=Path, help='Create a new JSON file; never overwrite')
    parser.add_argument('--schema-only', action='store_true',
                        help='Validate only the header of a live, unfinished log')
    args = parser.parse_args()
    before = args.log.stat()
    with args.log.open(encoding='utf-8') as stream:
        if args.schema_only:
            result = {'schemaVersion': 8, 'mode': 'liveHeaderOnly',
                      'regions': read_schema(stream),
                      'sourceBytesAtRead': before.st_size}
        else:
            result = summarize(stream)
    result['sourcePath'] = str(args.log)
    if not args.schema_only:
        with args.log.open('rb') as stream:
            result['sourceSha256'] = hashlib.file_digest(stream, 'sha256').hexdigest()
        after = args.log.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
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
