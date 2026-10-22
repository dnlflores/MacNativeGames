"""Installer diagnostics and watchdog tests; never contact or install onto a device."""
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('install_device', ROOT / 'games/totk/ios/tools/install_device.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class IOSInstaller(unittest.TestCase):
    def test_discovery_matches_exact_device(self):
        device = {'identifier': 'core-id', 'hardwareProperties': {'udid': 'IPAD-ID'}}
        document = {'result': {'devices': [device]}}
        self.assertEqual(installer.find_device(document, 'ipad-id'), device)
        self.assertIsNone(installer.find_device(document, 'another-device'))
        self.assertIsNone(installer.find_device({'error': {}}, 'ipad-id'))

    def test_lock_state(self):
        self.assertTrue(installer.locked({'device': {'isLocked': True}}))
        self.assertFalse(installer.locked({'device': {'isLocked': False}}))

    def test_installed_build_must_match_before_launch(self):
        document = {'result': {'apps': [{'bundleIdentifier': 'example.game', 'bundleVersion': '8'}]}}
        self.assertEqual(installer.verify_installed_build(document, 'example.game', '8'), '8')
        with self.assertRaisesRegex(installer.InstallFailure, 'reports build 8; expected build 9'):
            installer.verify_installed_build(document, 'example.game', '9')
        with self.assertRaisesRegex(installer.InstallFailure, 'not returned'):
            installer.verify_installed_build(document, 'missing.game', '8')

    def test_logs_output_and_exit_failure(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            path = Path(tmp)
            output = installer.run_step('1-Test output', [sys.executable, '-c', 'print("visible progress")'], 5, path, 'diagnostic hint')
            self.assertIn('visible progress', output)
            self.assertIn('visible progress', (path / '1-Test.log').read_text())
            with self.assertRaisesRegex(installer.InstallFailure, 'code 7.*\n.*diagnostic hint'):
                installer.run_step('2-Failure test', [sys.executable, '-c', 'raise SystemExit(7)'], 5, path, 'diagnostic hint')

    def test_watchdog_stops_hung_command(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            start = time.monotonic()
            with self.assertRaisesRegex(installer.InstallFailure, 'timed out'):
                installer.run_step('3-Timeout test', [sys.executable, '-c', 'import time; time.sleep(60)'], 0.2, Path(tmp), 'reconnect device', heartbeat=0.1)
            self.assertLess(time.monotonic() - start, 3)


if __name__ == '__main__':
    unittest.main()
