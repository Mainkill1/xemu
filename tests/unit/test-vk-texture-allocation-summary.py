#!/usr/bin/env python3
"""Validate lifecycle accounting and conservative configuration reuse analysis."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/performance/vk-texture-allocation-summary.py'


class AllocationSummaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('vk_allocation_summary', SCRIPT)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def records(self):
        config = {
            's_type': 14, 'flags': 0, 'image_type': 1, 'format': 37,
            'width': 4, 'height': 4, 'depth': 1, 'mip_levels': 1,
            'array_layers': 1, 'samples': 1, 'tiling': 0, 'usage': 6,
            'sharing_mode': 0, 'initial_layout': 0, 'allocation_flags': 0,
            'allocation_usage': 8, 'required_flags': 0, 'preferred_flags': 0,
            'memory_type_bits': 0, 'priority_bits': 0, 'min_alignment': 0,
        }
        first = {
            'type': 'create', 'seq': 1, 'frame': 0, 'submission': 1,
            'start_us': 100, 'elapsed_us': 7, 'result': 0,
            'surface_copy': False, 'image': '0000000000000001',
            'allocation': '0000000000000011', 'size_bytes': 64,
            'memory_type': 2, 'key_supported': True, 'pnext_present': False,
            'queue_family_count': 0, 'custom_pool': False, 'user_data': False,
            'config': config,
        }
        second = copy.deepcopy(first)
        second.update(seq=3, frame=1, submission=2, start_us=120,
                      elapsed_us=5, allocation='0000000000000012')
        return [
            {'type': 'schema', 'schema_version': 2,
             'duration_unit': 'host_elapsed_us',
             'frame_meaning': 'completed_renderer_flip_stalls',
             'scope': 'ordinary_texture_cache_including_surface_copy_excluding_dummy',
             'max_records': 10},
            first,
            {'type': 'destroy', 'seq': 2, 'frame': 1, 'submission': 2,
             'start_us': 110, 'elapsed_us': 3,
             'image': '0000000000000001', 'allocation': '0000000000000011',
             'teardown': False},
            second,
            {'type': 'destroy', 'seq': 4, 'frame': 2, 'submission': 3,
             'start_us': 130, 'elapsed_us': 4,
             'image': '0000000000000001', 'allocation': '0000000000000012',
             'teardown': True},
            {'type': 'summary', 'end_reason': 'renderer_teardown', 'records': 4, 'dropped': 0, 'complete': True,
             'clock_errors': 0, 'create_calls': 2, 'successful_creates': 2,
             'destroy_calls': 2, 'create_elapsed_total_us': 12,
             'destroy_elapsed_total_us': 7, 'create_elapsed_max_us': 7,
             'destroy_elapsed_max_us': 4},
        ]

    def summarize(self, records):
        return self.module.summarize(io.StringIO(
            ''.join(json.dumps(row) + '\n' for row in records)))

    def test_actual_calls_duration_bytes_and_handle_generation(self):
        result = self.summarize(self.records())
        self.assertEqual(result['creates'], {'calls': 2, 'successful': 2,
                                            'elapsedTotalMs': 0.012, 'elapsedMaxMs': 0.007})
        self.assertEqual(result['destroys'], {'calls': 2, 'nullCalls': 0,
                                             'elapsedTotalMs': 0.007, 'elapsedMaxMs': 0.004})
        self.assertEqual(result['totalCreatedBytes'], 128)
        self.assertEqual(result['peakLiveBytes'], 64)
        self.assertEqual(result['liveImagesAtEnd'], 0)
        self.assertEqual(result['configurationCount'], 1)
        self.assertEqual(result['unboundedReuseOpportunities'], 1)
        self.assertEqual(result['unboundedReuseBytes'], 64)
        self.assertIn('not a bounded pool hit rate', result['reuseMeaning'])

    def test_each_configuration_field_and_actual_memory_identity_matters(self):
        for field in self.records()[1]['config']:
            if field == 's_type':
                continue
            rows = self.records()
            rows[3]['config'][field] += 1
            with self.subTest(field=field):
                result = self.summarize(rows)
                self.assertEqual(result['unboundedReuseOpportunities'], 0)
                self.assertEqual(result['configurationCount'], 2)
        for field in ['memory_type', 'size_bytes']:
            rows = self.records(); rows[3][field] += 1
            with self.subTest(field=field):
                self.assertEqual(self.summarize(rows)['unboundedReuseOpportunities'], 0)

    def test_surface_copy_and_unsupported_keys_are_excluded(self):
        for field in ['surface_copy', 'unsupported']:
            rows = self.records()
            if field == 'surface_copy': rows[3]['surface_copy'] = True
            else: rows[3].update(key_supported=False, pnext_present=True)
            with self.subTest(field=field):
                result = self.summarize(rows)
                self.assertEqual(result['unboundedReuseOpportunities'], 0)
                self.assertEqual(result['excludedSuccessfulCreates'], 1)

    def test_unsupported_records_still_validate_all_metadata(self):
        for field, value in [('custom_pool', None), ('user_data', 1),
                             ('queue_family_count', True)]:
            rows = self.records()
            rows[3].update(key_supported=False, pnext_present=True)
            rows[3][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.summarize(rows)

    def test_missing_and_inconsistent_footer_rejected(self):
        variants = [self.records()[:-1]]
        for field, value in [('complete', False), ('dropped', 1), ('clock_errors', 1),
                             ('create_calls', 3), ('records', 3),
                             ('create_elapsed_total_us', 11), ('destroy_elapsed_max_us', 3)]:
            rows = self.records(); rows[-1][field] = value; variants.append(rows)
        variants.append(self.records() + [self.records()[1]])
        for rows in variants:
            with self.subTest(footer=rows[-1]), self.assertRaises(ValueError):
                self.summarize(rows)

    def test_shutdown_checkpoint_reports_live_images_without_inventing_destroys(self):
        rows = self.records()
        rows.pop(4)
        rows[-1].update(end_reason='shutdown_checkpoint', records=3,
                        destroy_calls=1, destroy_elapsed_total_us=3,
                        destroy_elapsed_max_us=3)
        result = self.summarize(rows)
        self.assertEqual(result['endReason'], 'shutdown_checkpoint')
        self.assertEqual(result['liveImagesAtEnd'], 1)
        self.assertEqual(result['liveBytesAtEnd'], 64)
        self.assertEqual(result['destroys']['calls'], 1)
        self.assertEqual(result['creates']['calls'], 2)
        self.assertIn('checkpoint', result['lifecycleCoverage'])
        rows[-1]['end_reason'] = 'renderer_teardown'
        with self.assertRaises(ValueError):
            self.summarize(rows)
        rows[-1]['end_reason'] = 'unknown'
        with self.assertRaises(ValueError):
            self.summarize(rows)

    def test_gaps_order_and_lifetime_errors_rejected(self):
        mutations = [(1, 'seq', 2), (3, 'seq', 4), (3, 'frame', 0),
                     (3, 'start_us', 105), (4, 'allocation', '0000000000000011')]
        for index, field, value in mutations:
            rows = self.records(); rows[index][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.summarize(rows)
        rows = self.records(); rows.pop(2); rows[2]['seq'] = 2
        with self.assertRaises(ValueError): self.summarize(rows)

    def test_bad_types_schema_and_config_rejected(self):
        for field, value in [('elapsed_us', True), ('elapsed_us', -1),
                             ('size_bytes', 1.5), ('surface_copy', 1),
                             ('image', 'bad'), ('key_supported', False)]:
            rows = self.records(); rows[1][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.summarize(rows)
        rows = self.records(); del rows[1]['config']['min_alignment']
        with self.assertRaises(ValueError): self.summarize(rows)
        for field, value in [('schema_version', 3), ('duration_unit', 'ticks'),
                             ('scope', 'all_images'), ('max_records', 1)]:
            rows = self.records(); rows[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.summarize(rows)

    def test_incomplete_and_oversized_records_rejected(self):
        for text in ['', '{', json.dumps(self.records()[0]) + '\n',
                     json.dumps(self.records()[0]) + '\n' + 'x' * 65537 + '\n']:
            with self.subTest(size=len(text)), self.assertRaises(ValueError):
                self.module.summarize(io.StringIO(text))

    def test_live_header_does_not_claim_complete_lifecycle(self):
        stream = io.StringIO(json.dumps(self.records()[0]) + '\n' + '{unfinished')
        result = self.module.read_schema(stream)
        self.assertEqual(result['schemaVersion'], 2)
        self.assertEqual(result['mode'], 'liveHeaderOnly')
        self.assertNotIn('creates', result)
        self.assertEqual(stream.read(), '{unfinished')

    def test_cli_hash_and_existing_output_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'trace.jsonl'; output = Path(directory) / 'report.json'
            source.write_text(''.join(json.dumps(row) + '\n' for row in self.records()))
            run = subprocess.run(['python3', str(SCRIPT), str(source), '--out', str(output)],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(json.loads(output.read_text())['sourceBytes'], source.stat().st_size)
            self.assertEqual(len(json.loads(output.read_text())['sourceSha256']), 64)
            output.write_text('keep')
            run = subprocess.run(['python3', str(SCRIPT), str(source), '--out', str(output)],
                                 capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertEqual(output.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
