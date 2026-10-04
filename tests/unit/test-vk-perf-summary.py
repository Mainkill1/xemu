#!/usr/bin/env python3
"""Check the offline schema-8 reader against valid and damaged telemetry."""
import importlib.util
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/performance/vk-perf-summary.py'


class SummaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('vk_perf_summary', SCRIPT)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def records(self):
        return [
            {'type': 'schema', 'schema_version': 8,
             'cpu_regions': ['bind_textures', 'texture_upload']},
            {'type': 'frame', 'schema_version': 8, 'guest_frame': 1,
             'timestamp_us': 100, 'cpu_region_calls_per_guest_frame': [2, 1],
             'cpu_region_us_per_guest_frame': [4000, 3000]},
            {'type': 'frame', 'schema_version': 8, 'guest_frame': 2,
             'timestamp_us': 1100, 'cpu_region_calls_per_guest_frame': [4, 0],
             'cpu_region_us_per_guest_frame': [1000, 0]},
        ]

    def summarize(self, rows):
        return self.module.summarize(io.StringIO(
            ''.join(json.dumps(x) + '\n' for x in rows)))

    @unittest.skipIf(os.name == 'nt', 'Windows refuses replacement of open files')
    def test_replacement_with_same_size_and_time_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'trace.jsonl'
            replacement = Path(directory) / 'replacement.jsonl'
            source.write_text(''.join(json.dumps(x) + '\n' for x in self.records()))
            original = source.stat()
            replacement.write_text(source.read_text().replace('4000', '5000'))
            self.assertEqual(source.stat().st_size, replacement.stat().st_size)
            os.utime(replacement, ns=(original.st_atime_ns, original.st_mtime_ns))
            summarize = self.module.summarize

            def swap_after_parse(stream):
                result = summarize(stream)
                replacement.replace(source)
                return result

            with patch.object(self.module, 'summarize', swap_after_parse), \
                    patch.object(sys, 'argv', [str(SCRIPT), str(source)]), \
                    patch.object(sys, 'stdout', io.StringIO()):
                with self.assertRaisesRegex(ValueError, 'changed'):
                    self.module.main()

    def test_finalized_hash_without_python_311_file_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'trace.jsonl'
            text = ''.join(json.dumps(x) + '\n' for x in self.records())
            source.write_text(text)
            output = io.StringIO()
            with patch.object(hashlib, 'file_digest', None, create=True), \
                    patch.object(sys, 'argv', [str(SCRIPT), str(source)]), \
                    patch.object(sys, 'stdout', output):
                self.module.main()
            result = json.loads(output.getvalue())
            self.assertEqual(result['sourceSha256'],
                             hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertEqual(result['frameRecords'], 2)

    def test_elapsed_units_and_nested_regions_are_independent(self):
        result = self.summarize(self.records())
        self.assertEqual(result['frameRecords'], 2)
        self.assertEqual(result['observedTimestampSpanMs'], 1)
        self.assertEqual(result['regions']['bind_textures'],
                         {'calls': 6, 'elapsedTotalMs': 5,
                          'maximumFrameElapsedMs': 4,
                          'maximumFrame': 1})
        self.assertEqual(result['regions']['texture_upload']['elapsedTotalMs'], 3)
        self.assertNotIn('allocationTimeMs', result)
        self.assertIn('nested', result['durationMeaning'])

    def test_missing_or_truncated_records_fail(self):
        for text in ['', json.dumps(self.records()[0]) + '\n',
                     ''.join(json.dumps(x) + '\n' for x in self.records()) + '{']:
            with self.subTest(text=text[-30:]), self.assertRaises(ValueError):
                self.module.summarize(io.StringIO(text))

    def test_unsupported_schema_and_duplicate_names_fail(self):
        for key, value in [('schema_version', 9),
                           ('cpu_regions', ['bind_textures', 'bind_textures'])]:
            rows = self.records(); rows[0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.summarize(rows)

    def test_bad_counter_arrays_fail(self):
        for values in [[1], [1, -1], [1, True], [1, 1.5], [1, None]]:
            rows = self.records(); rows[1]['cpu_region_us_per_guest_frame'] = values
            with self.subTest(values=values), self.assertRaises(ValueError):
                self.summarize(rows)

    def test_missing_field_and_wrong_frame_schema_fail(self):
        for change in ['missing', 'wrong-schema', 'extra-schema']:
            rows = self.records()
            if change == 'missing': del rows[1]['cpu_region_calls_per_guest_frame']
            if change == 'wrong-schema': rows[1]['schema_version'] = 7
            if change == 'extra-schema': rows[2] = rows[0]
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.summarize(rows)

    def test_clock_or_frame_regression_fails(self):
        for key in ['timestamp_us', 'guest_frame']:
            rows = self.records(); rows[2][key] = 0
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.summarize(rows)

    def test_missing_initial_or_interior_frame_fails(self):
        for change in ['missing-first', 'missing-interior']:
            rows = self.records()
            if change == 'missing-first':
                rows[1]['guest_frame'] = 2
                rows[2]['guest_frame'] = 3
            else:
                rows[2]['guest_frame'] = 3
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.summarize(rows)

    def test_oversized_line_fails_without_full_read(self):
        with self.assertRaises(ValueError):
            self.module.summarize(io.StringIO(' ' * 65537 + '\n'))

    def test_schema_only_does_not_read_unfinished_live_body(self):
        class HeaderStream(io.StringIO):
            def readline(self, limit=-1):
                if self.tell() != 0:
                    raise AssertionError('Live body must not be read')
                self_limit = 65537
                if limit != self_limit:
                    raise AssertionError('Header read must be bounded')
                return super().readline(limit)
        text = json.dumps(self.records()[0]) + '\n' + '{unfinished'
        names = self.module.read_schema(HeaderStream(text))
        self.assertEqual(names, ['bind_textures', 'texture_upload'])

    def test_streamed_log_larger_than_runner_text_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'large.jsonl'
            schema, frame, _ = self.records()
            with path.open('w') as stream:
                stream.write(json.dumps(schema) + '\n')
                for i in range(1, 100001):
                    frame.update(guest_frame=i, timestamp_us=i * 1000)
                    stream.write(json.dumps(frame) + '\n')
            self.assertGreater(path.stat().st_size, 16 * 1024 * 1024)
            with path.open() as stream:
                result = self.module.summarize(stream)
            self.assertEqual(result['frameRecords'], 100000)
            self.assertEqual(result['regions']['bind_textures']['elapsedTotalMs'], 400000)

    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'result.json'; path.write_text('original')
            with self.assertRaises(FileExistsError):
                self.module.write_report(path, {'schemaVersion': 8})
            self.assertEqual(path.read_text(), 'original')

    def auxiliary_records(self):
        names = ['pvideo_upload', 'display_render', 'surface_download',
                 'surface_create', 'surface_upload', 'texture_upload',
                 'dummy_texture_create']
        rows = [{'type': 'schema', 'schema_version': 8,
                 'single_time_callers': names}]
        for frame, timestamp, calls in [(1, 0, 1), (2, 2000000, 2),
                                         (3, 4000000, 7), (4, 5000000, 3)]:
            rows.append({
                'type': 'frame', 'schema_version': 8, 'guest_frame': frame,
                'timestamp_us': timestamp,
                'single_time_submit_count_per_guest_frame': [0, 0, 0, 0, calls, 0, 0],
                'single_time_timed_submit_count_per_guest_frame': [0, 0, 0, 0, calls, 0, 0],
                'queue_wait_idle_count_per_guest_frame': [0, 0, 0, 0, calls, 0, 0],
                'single_time_sampled_submit_cpu_us_per_guest_frame': [0, 0, 0, 0, calls * 100, 0, 0],
                'single_time_sampled_wait_us_per_guest_frame': [0, 0, 0, 0, calls * 200, 0, 0],
            })
        return rows

    def summarize_auxiliary(self, rows, tail_seconds=2):
        summarize = getattr(self.module, 'summarize_auxiliary', None)
        self.assertTrue(callable(summarize), 'Auxiliary window analysis is not implemented')
        return summarize(io.StringIO(
            ''.join(json.dumps(x) + '\n' for x in rows)), tail_seconds)

    def test_auxiliary_tail_does_not_credit_a_straddling_bucket(self):
        # Including the bucket (2s,4s] in the (3s,5s] window would invent
        # seven in-window calls; the reader cannot locate those events.
        result = self.summarize_auxiliary(self.auxiliary_records())
        all_calls = result['all']['callers']['surface_upload']
        self.assertEqual(all_calls['submits'], 13)
        self.assertEqual(all_calls['submitHostElapsedMs'], 1.3)
        self.assertEqual(all_calls['queueIdleWaitHostElapsedMs'], 2.6)
        tail = result['tail']
        self.assertEqual(tail['startTimestampUs'], 3000000)
        self.assertEqual(tail['endTimestampUs'], 5000000)
        self.assertEqual(tail['completeBucketCount'], 1)
        self.assertEqual(tail['completeBuckets']['surface_upload']['submits'], 3)
        self.assertEqual(tail['straddlingBucket']['guestFrame'], 3)
        self.assertEqual(tail['straddlingBucket']['callers']['surface_upload']['submits'], 7)
        self.assertEqual(tail['completeBuckets']['display_render']['submits'], 0)
        self.assertNotIn('improvementPercent', result)

    def test_auxiliary_short_or_nonpositive_window_is_not_empty_success(self):
        for seconds in (6, 0, -1, True, 1.5):
            with self.subTest(seconds=seconds), self.assertRaises(ValueError):
                self.summarize_auxiliary(self.auxiliary_records(), seconds)

    def test_auxiliary_exact_boundary_excludes_previous_bucket(self):
        result = self.summarize_auxiliary(self.auxiliary_records(), 3)['tail']
        self.assertEqual(result['startTimestampUs'], 2000000)
        self.assertEqual(result['completeBucketCount'], 2)
        self.assertEqual(result['completeBuckets']['surface_upload']['submits'], 10)
        self.assertIsNone(result['straddlingBucket'])
        # Starting at the first timestamp still cannot locate the first
        # bucket's one submit, because its beginning is not recorded.
        result = self.summarize_auxiliary(self.auxiliary_records(), 5)['tail']
        self.assertEqual(result['completeBucketCount'], 3)
        self.assertEqual(result['completeBuckets']['surface_upload']['submits'], 12)
        self.assertIsNone(result['straddlingBucket'])

    def test_auxiliary_missing_or_contradictory_counters_fail(self):
        field = 'single_time_submit_count_per_guest_frame'
        for change in ('missing', 'duplicate-name', 'unknown-name',
                       'short-array', 'bool-counter', 'wait-without-submit'):
            rows = self.auxiliary_records()
            if change == 'missing': del rows[1][field]
            if change == 'duplicate-name': rows[0]['single_time_callers'][1] = 'pvideo_upload'
            if change == 'unknown-name': rows[0]['single_time_callers'][1] = 'unknown'
            if change == 'short-array': rows[1][field] = [0]
            if change == 'bool-counter': rows[1][field][4] = True
            if change == 'wait-without-submit': rows[1][field][4] = 0
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.summarize_auxiliary(rows)


if __name__ == '__main__':
    unittest.main()
