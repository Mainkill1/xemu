#!/usr/bin/env python3
"""Check the offline schema-8 reader against valid and damaged telemetry."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

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


if __name__ == '__main__':
    unittest.main()
