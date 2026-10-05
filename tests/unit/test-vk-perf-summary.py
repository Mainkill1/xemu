#!/usr/bin/env python3
"""Check the offline schema-8 reader against valid and damaged telemetry."""
import hashlib
import importlib.util
import io
import os
import json
from pathlib import Path
import tempfile
import subprocess
import sys
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

    def test_digest_binds_consumed_bytes_after_in_place_rewrite(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'trace.jsonl'
            original_bytes = (''.join(json.dumps(x) + '\r\n'
                                      for x in self.records())).encode('utf-8')
            source.write_bytes(original_bytes)
            original_stat = source.stat()
            summarize = self.module.summarize

            def rewrite_after_parse(stream):
                result = summarize(stream)
                source.write_bytes(original_bytes.replace(b'4000', b'5000'))
                os.utime(source, ns=(original_stat.st_atime_ns,
                                    original_stat.st_mtime_ns))
                return result

            output = io.StringIO()
            with patch.object(self.module, 'summarize', rewrite_after_parse), \
                    patch.object(sys, 'argv', [str(SCRIPT), str(source)]), \
                    patch.object(sys, 'stdout', output):
                self.module.main()
            result = json.loads(output.getvalue())
            self.assertEqual(result['regions']['bind_textures']['elapsedTotalMs'], 5)
            self.assertEqual(result['sourceSha256'],
                             hashlib.sha256(original_bytes).hexdigest())
            self.assertEqual(result['sourceBytes'], len(original_bytes))

    def test_schema_only_cli_does_not_decode_body(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'live.jsonl'
            source.write_bytes((json.dumps(self.records()[0]) + '\n').encode()
                               + b'\xffunfinished')
            result = subprocess.run([sys.executable, str(SCRIPT), str(source),
                                     '--schema-only'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['mode'], 'liveHeaderOnly')
            self.assertNotIn('sourceSha256', report)

    def test_json_duplicate_keys_and_non_json_constants_are_rejected(self):
        for text in ['{"type":"schema","type":"schema"}\n',
                     '{"nested":{"hot_stride":999,"hot_stride":16}}\n',
                     '{"unused":NaN}\n', '{"unused":Infinity}\n',
                     '{"unused":-Infinity}\n']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.module.read_record(io.StringIO(text))

    def test_cpu_frame_schema_requires_integer(self):
        rows = self.records(); rows[1]['schema_version'] = 8.0
        with self.assertRaises(ValueError):
            self.summarize(rows)

    def test_cpu_elapsed_requires_a_call(self):
        rows = self.records(); rows[1]['cpu_region_calls_per_guest_frame'][0] = 0
        with self.assertRaises(ValueError):
            self.summarize(rows)

    def test_individual_counters_are_uint64_but_totals_can_exceed_it(self):
        for rows, summarize, field in [
                (self.records(), self.summarize,
                 'cpu_region_calls_per_guest_frame'),
                (self.auxiliary_records(), self.summarize_auxiliary,
                 'single_time_submit_count_per_guest_frame'),
                (self.finish_records(), self.summarize_finish,
                 'finish_count_per_guest_frame')]:
            rows[1][field][0] = 2 ** 64
            if field == 'single_time_submit_count_per_guest_frame':
                for key in ('single_time_timed_submit_count_per_guest_frame',
                            'queue_wait_idle_count_per_guest_frame'):
                    rows[1][key][0] = 2 ** 64
            with self.subTest(field=field), self.assertRaises(ValueError):
                summarize(rows)
        rows = self.records()
        for row in rows[1:]:
            row['cpu_region_calls_per_guest_frame'][0] = 2 ** 64 - 1
        result = self.summarize(rows)
        self.assertEqual(result['regions']['bind_textures']['calls'],
                         2 * (2 ** 64 - 1))

    def test_frozen_recipes_pin_their_archived_reader(self):
        recipes = SCRIPT.parents[2] / 'docs/performance/recipes'
        source = json.loads((recipes / 'vk-perf-reader-v1.json').read_text())
        self.assertEqual(source['sourceRevision'],
                         '3e69f0b8f110430d34c796706bde35d3c05d3e16')
        for name in ('pgr2-finish-300', 'pgr2-host-profile-30'):
            job = json.loads((recipes / name / 'job.json').read_text())
            pin = next(x for x in job['inputs'] if x['path'] == 'vk-perf-summary.py')
            self.assertEqual(source['sourceSha256'], pin['expectedSha256'])
            instructions = (recipes / name / 'README.md').read_text()
            self.assertIn('vk-perf-reader-v1.json', instructions)
        instructions = (recipes / 'pgr2-finish-300/README.md').read_text()
        self.assertIn(source['sourceRevision'] + ':' + source['sourcePath'],
                      instructions)
        self.assertIn(source['sourceSha256'], instructions)

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

    def finish_records(self):
        names = ['vertex_buffer_dirty', 'surface_create', 'surface_down',
                 'need_buffer_space', 'framebuffer_dirty', 'presenting',
                 'flip_stall', 'flush', 'stalled', 'texture_dirty']
        rows = [{'type': 'schema', 'schema_version': 8, 'finish_reasons': names,
                 'duration_sampling': {'initial_per_reason_per_frame': 8,
                                       'hot_stride': 16}}]
        # Timed submissions are a subset of submitted calls: some finishes
        # have no command buffer, and sampling uses call ordinal, not submit.
        for frame, timestamp, calls, submits, timed in [
                (1, 0, 10, 9, 8), (2, 2000000, 2, 2, 2),
                (3, 4000000, 20, 7, 3), (4, 5000000, 12, 2, 1)]:
            row = {'type': 'frame', 'schema_version': 8, 'guest_frame': frame,
                   'timestamp_us': timestamp}
            for field, value in [
                    ('finish_count_per_guest_frame', calls),
                    ('finish_submit_count_per_guest_frame', submits),
                    ('finish_timed_submit_count_per_guest_frame', timed),
                    ('fence_wait_count_per_guest_frame', submits),
                    ('finish_sampled_submit_cpu_us_per_guest_frame', timed * 100),
                    ('finish_sampled_wait_us_per_guest_frame', timed * 200)]:
                row[field] = [0, 0, 0, value, 0, 0, 0, 0, 0, 0]
            rows.append(row)
        return rows

    def summarize_finish(self, rows, seconds=2):
        function = getattr(self.module, 'summarize_finish', None)
        self.assertTrue(callable(function), 'Finish sampling/window analysis is not implemented')
        return function(io.StringIO(''.join(json.dumps(x) + '\n' for x in rows)), seconds)

    def test_finish_partial_samples_are_not_scaled_to_total_wait(self):
        result = self.summarize_finish(self.finish_records())
        stats = result['all']['reasons']['need_buffer_space']
        self.assertEqual(stats, {'calls': 44, 'submits': 20, 'timingSamples': 14,
                                 'fenceWaits': 20, 'sampledSubmitHostElapsedMs': 1.4,
                                 'sampledFenceWaitHostElapsedMs': 2.8,
                                 'durationCoverage': 'partial'})
        self.assertNotIn('estimatedWaitMs', stats)
        self.assertNotIn('improvementPercent', result)
        self.assertIn('sampled', result['durationMeaning'])

    def test_finish_tail_keeps_crossing_bucket_separate(self):
        tail = self.summarize_finish(self.finish_records())['tail']
        self.assertEqual(tail['completeBucketCount'], 1)
        self.assertEqual(tail['completeBuckets']['need_buffer_space']['submits'], 2)
        self.assertEqual(tail['completeBuckets']['need_buffer_space']['sampledFenceWaitHostElapsedMs'], 0.2)
        self.assertEqual(tail['straddlingBucket']['guestFrame'], 3)
        self.assertEqual(tail['straddlingBucket']['reasons']['need_buffer_space']['submits'], 7)

    def test_finish_exact_start_and_first_bucket_are_not_credited(self):
        tail = self.summarize_finish(self.finish_records(), 3)['tail']
        self.assertEqual(tail['completeBucketCount'], 2)
        self.assertEqual(tail['completeBuckets']['need_buffer_space']['submits'], 9)
        self.assertIsNone(tail['straddlingBucket'])
        tail = self.summarize_finish(self.finish_records(), 5)['tail']
        self.assertEqual(tail['completeBuckets']['need_buffer_space']['submits'], 11)
        self.assertIsNone(tail['straddlingBucket'])

    def test_finish_names_sampling_and_counter_contradictions_fail_closed(self):
        for change in ('unknown', 'duplicate', 'sampling', 'missing', 'short',
                       'bool', 'submits', 'samples', 'waits', 'time-no-sample'):
            rows = self.finish_records()
            if change == 'unknown': rows[0]['finish_reasons'][0] = 'unknown'
            if change == 'duplicate': rows[0]['finish_reasons'][0] = 'surface_create'
            if change == 'sampling': rows[0]['duration_sampling']['hot_stride'] = True
            if change == 'missing': del rows[1]['finish_sampled_wait_us_per_guest_frame']
            if change == 'short': rows[1]['finish_count_per_guest_frame'] = [10]
            if change == 'bool': rows[1]['finish_count_per_guest_frame'][3] = True
            if change == 'submits': rows[1]['finish_submit_count_per_guest_frame'][3] = 11
            if change == 'samples': rows[1]['finish_timed_submit_count_per_guest_frame'][3] = 10
            if change == 'waits': rows[1]['fence_wait_count_per_guest_frame'][3] = 8
            if change == 'time-no-sample': rows[1]['finish_timed_submit_count_per_guest_frame'][3] = 0
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.summarize_finish(rows)

    def test_finish_required_sample_lower_bound(self):
        # Unknown non-submitting ordinals may consume scheduled samples.
        # Hybrid tracing can add samples; it cannot remove required ones.
        for calls, submits, samples, valid in [
                (0, 0, 0, True), (1, 1, 0, False), (8, 8, 7, False),
                (9, 8, 7, True), (9, 9, 7, False), (16, 16, 8, False),
                (16, 15, 8, True), (16, 16, 16, True), (20, 7, 0, True)]:
            rows = self.finish_records()[:2]
            values = {
                'finish_count_per_guest_frame': calls,
                'finish_submit_count_per_guest_frame': submits,
                'finish_timed_submit_count_per_guest_frame': samples,
                'fence_wait_count_per_guest_frame': submits,
                'finish_sampled_submit_cpu_us_per_guest_frame': 0,
                'finish_sampled_wait_us_per_guest_frame': 0,
            }
            for field, value in values.items():
                rows[1][field][3] = value
            with self.subTest(calls=calls, submits=submits, samples=samples):
                if valid:
                    self.summarize_finish(rows, None)
                else:
                    with self.assertRaisesRegex(ValueError, 'sampling'):
                        self.summarize_finish(rows, None)

    def test_finish_missing_frames_clock_regression_or_short_tail_fail(self):
        for change in ('first', 'interior', 'clock', 'schema'):
            rows = self.finish_records()
            if change == 'first': rows[1]['guest_frame'] = 2
            if change == 'interior': rows[2]['guest_frame'] = 3
            if change == 'clock': rows[3]['timestamp_us'] = 1
            if change == 'schema': rows[1]['schema_version'] = True
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.summarize_finish(rows)
        for seconds in (6, 0, -1, True):
            with self.subTest(seconds=seconds), self.assertRaises(ValueError):
                self.summarize_finish(self.finish_records(), seconds)

    def test_finish_all_timed_and_zero_submissions_have_distinct_coverage(self):
        rows = self.finish_records()
        rows[1]['finish_timed_submit_count_per_guest_frame'][3] = 8
        # A reason with exactly two calls/submits and timing samples is exact.
        for field in rows[2]:
            if isinstance(rows[2][field], list):
                rows[2][field][0] = rows[2][field][3]
        result = self.summarize_finish(rows)['all']['reasons']
        self.assertEqual(result['vertex_buffer_dirty']['durationCoverage'], 'all_submissions')
        self.assertEqual(result['surface_down']['durationCoverage'], 'no_submissions')
        self.assertEqual(result['need_buffer_space']['durationCoverage'], 'partial')

    def test_finish_cli_writes_new_report_and_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input.jsonl'
            path.write_text(''.join(json.dumps(x) + '\n' for x in self.finish_records()))
            out = Path(directory) / 'out.json'
            args = [sys.executable, str(SCRIPT), str(path), '--finish', '--tail-seconds', '2', '--out', str(out)]
            first = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            original = out.read_bytes()
            self.assertEqual(json.loads(original)['all']['reasons']['need_buffer_space']['submits'], 20)
            self.assertNotEqual(subprocess.run(args, capture_output=True).returncode, 0)
            self.assertEqual(out.read_bytes(), original)

    def test_finish_cli_rejects_live_header_or_auxiliary_combination(self):
        for other in ('--schema-only', '--auxiliary'):
            result = subprocess.run([sys.executable, str(SCRIPT), 'unused.jsonl', '--finish', other], capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_finalized_cli_supports_python_without_file_digest(self):
        # Supported Python 3.9/3.10 has no hashlib.file_digest. Exercise the
        # real CLI with that API absent, rather than mocking the hash output.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input.jsonl'
            path.write_text(''.join(json.dumps(x) + '\n' for x in self.finish_records()))
            code = ('import hashlib,runpy,sys; '
                    'hashlib.__dict__.pop("file_digest",None); '
                    'script=sys.argv.pop(1); runpy.run_path(script,run_name="__main__")')
            result = subprocess.run([sys.executable, '-c', code, str(SCRIPT), str(path), '--finish'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            import hashlib
            self.assertEqual(json.loads(result.stdout)['sourceSha256'],
                             hashlib.sha256(path.read_bytes()).hexdigest())


if __name__ == '__main__':
    unittest.main()
