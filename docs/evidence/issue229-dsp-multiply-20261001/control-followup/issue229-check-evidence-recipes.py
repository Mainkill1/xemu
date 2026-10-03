"""Negative qualification/retention fixtures; never modify real campaign data."""
from pathlib import Path
import copy
import json
import subprocess
import tempfile
import unittest

BASE = Path('/home/codex/src/steamdeck-xemu')
SOURCE = BASE / 'artifacts/issue229-dsp-multiply/native-balanced-v3'
SCRIPTS = Path(__file__).resolve().parent

class Recipes(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=BASE / 'scratch')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'artifacts/issue229-dsp-multiply/native-balanced-v3'
        self.source.mkdir(parents=True)
        self.destination = self.root / 'worktrees/issue229-dsp-multiply/docs/evidence/issue229-dsp-multiply-20261001/control-followup/native-balanced-v3'
        self.destination.mkdir(parents=True)
        self.plan = json.loads((SOURCE / 'plan.json').read_text())
        self.put(self.source / 'plan.json', self.plan)
        reference = next((SOURCE / 'collected').glob('*/result.json')).parent
        self.directories = []
        for i, attempt in enumerate(self.plan['attempts']):
            d = self.source / 'collected' / str(i)
            d.mkdir(parents=True)
            for name in ['result.json', 'job.json', 'input-manifest.json', 'launch.json', 'performance.json', 'diagnostics/run-state/report.json', 'runtime-cleanup.json']:
                value = json.loads((reference / name).read_text())
                if name == 'result.json':
                    value.update(job=attempt['id'], executableSha256=attempt['executableSha256'])
                elif name == 'job.json':
                    draft = json.loads((SOURCE / (attempt['id'] + '-draft.json')).read_text())['job']
                    # Runner serializer differs in key case only.
                    value = draft
                elif name == 'input-manifest.json':
                    value['ExecutableSha256'] = attempt['executableSha256']
                    for row in value['Inputs']:
                        if row['Path'] == 'xemu':
                            row.update(Sha256=attempt['executableSha256'], ExpectedSha256=attempt['executableSha256'])
                self.put(d / name, value)
            self.directories.append(d)
        for path in SOURCE.glob('*-draft.json'):
            (self.source / path.name).write_bytes(path.read_bytes())

    def put(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value))

    def run_recipe(self, name):
        code = (SCRIPTS / name).read_text().replace(str(BASE), str(self.root))
        result = subprocess.run(['python3', '-c', code], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def summarize(self):
        self.run_recipe('issue229-summarize-native-balanced-v3.py')
        return json.loads((self.source / 'summary.json').read_text())

    def test_matching_contracts_compare(self):
        self.assertEqual(len(self.summarize()['comparisons']), 15)

    def test_different_profiles_do_not_compare(self):
        p = self.directories[-1] / 'performance.json'
        v = json.loads(p.read_text()); v['ProfileSha256'] = 'different'; self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])

    def test_different_storage_does_not_compare(self):
        p = self.directories[-1] / 'diagnostics/run-state/report.json'
        v = json.loads(p.read_text()); v['ContractSha256'] = 'different'; self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])

    def test_changed_workload_does_not_compare(self):
        p = self.directories[-1] / 'job.json'
        v = json.loads(p.read_text()); v['timeoutSeconds'] += 1; self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])

    def test_changed_host_does_not_compare(self):
        p = self.directories[-1] / 'launch.json'
        v = json.loads(p.read_text()); v['host']['runnerVersion'] = 'different'; self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])

    def test_changed_fixed_input_does_not_compare(self):
        p = self.directories[-1] / 'input-manifest.json'
        v = json.loads(p.read_text()); v['RuntimeFiles'][0]['Sha256'] = 'different'; self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])

    def test_early_failure_retained_and_not_compared(self):
        d = self.directories[-1]
        for p in list(d.rglob('*')):
            if p.is_file() and p.name != 'result.json':
                p.unlink()
        p = d / 'result.json'; v = json.loads(p.read_text())
        v.update(status='launch_failed', exitCode=None, assessment={'Comparison':'ineligible'})
        self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])
        self.run_recipe('issue229-package-native-balanced-v3.py')
        target = self.destination / 'raw' / self.plan['attempts'][-1]['id']
        self.assertTrue((target / 'artifacts.tar.gz').exists())
        self.assertIn('runtime-cleanup.json', json.loads((target / 'retention.json').read_text())['missingReadableFiles'])

    def test_preflight_failure_without_executable_identity_is_retained(self):
        p = self.directories[-1] / 'result.json'
        v = json.loads(p.read_text())
        v.update(status='invalid', executableSha256=None, assessment={'Comparison':'ineligible'})
        self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])

    def test_rejected_executable_identity_is_retained(self):
        p = self.directories[-1] / 'result.json'
        v = json.loads(p.read_text())
        v.update(status='invalid', executableSha256='rejected', assessment={'Comparison':'ineligible'})
        self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])

    def test_invalid_job_json_is_retained(self):
        (self.directories[-1] / 'job.json').write_text('{invalid')
        p = self.directories[-1] / 'result.json'
        v = json.loads(p.read_text())
        v.update(status='invalid', assessment={'Comparison':'ineligible'})
        self.put(p, v)
        self.assertEqual(self.summarize()['comparisons'], [])
        self.run_recipe('issue229-package-native-balanced-v3.py')

    def test_unreadable_cleanup_retains_raw_failure(self):
        (self.directories[-1] / 'runtime-cleanup.json').write_text('{interrupted')
        self.run_recipe('issue229-package-native-balanced-v3.py')
        target = self.destination / 'raw' / self.plan['attempts'][-1]['id']
        self.assertIsNone(json.loads((target / 'retention.json').read_text())['targetStopped'])
        self.assertTrue((target / 'artifacts.tar.gz').exists())

    def test_partial_publication_is_preserved_and_repaired(self):
        target = self.destination / 'raw' / self.plan['attempts'][0]['id']
        target.mkdir(parents=True)
        (target / 'interrupted-marker').write_text('keep me')
        self.run_recipe('issue229-package-native-balanced-v3.py')
        self.assertTrue((target / 'inventory.json').exists())
        self.assertTrue(any((p / 'interrupted-marker').exists() for p in target.parent.glob(target.name + '.partial-*')))
        self.run_recipe('issue229-package-native-balanced-v3.py')

if __name__ == '__main__':
    unittest.main()
