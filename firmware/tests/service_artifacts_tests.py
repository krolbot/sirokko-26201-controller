#!/usr/bin/env python3
"""Offline tool regression; generated key is temporary, never a device/deployment key."""
import base64
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/service_artifacts.py'


class ServiceArtifactsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='service-test-', dir=ROOT / 'build-host')
        cls.directory = Path(cls.temp.name)
        cls.key = cls.directory / 'temporary-test-key.pem'
        fd = os.open(cls.key, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:3072'],
                           stdout=stream, stderr=subprocess.DEVNULL, check=True)
        cls.public = cls.directory / 'public.pem'
        subprocess.run(['openssl', 'pkey', '-in', str(cls.key), '-pubout', '-out', str(cls.public)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
        cls.image = cls.directory / 'synthetic-signature-test.bin'
        cls.image.write_bytes(b'NOT AN ESP IMAGE: OFFLINE SIGNATURE FIXTURE\n' * 64)
        cls.telemetry = cls.directory / 'telemetry.json'
        cls.telemetry.write_text(json.dumps(dict(board='ESP32-S3-DevKitC-1-N8R8-v1.1',
            version='0.1.7-DRAFT', nonce='0123456789abcdef0123456789abcdef', uptime_ms=10000, last_sequence=0)))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_tool(self, *args, ok=True):
        result = subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, ok, 'CLI status differs from expected')
        return result

    def test_signed_request_is_accepted_by_production_parser_and_RSA(self):
        signed = self.directory / 'signed.json'
        self.run_tool('sign', '--image', self.image, '--version', '0.1.8-DRAFT',
                      '--url', 'https://updates.example.test/image.bin', '--key', self.key, '--output', signed)
        request = self.directory / 'request.json'
        self.run_tool('request', '--operation', 'update', '--telemetry', self.telemetry,
                      '--manifest', signed, '--output', request)
        adapter = ROOT / 'build-host/network_tests'
        result = subprocess.run([str(adapter), '--verify-request', str(request), str(self.public)], capture_output=True)
        self.assertEqual(result.returncode, 0)
        altered = json.loads(request.read_text()); altered['sha256'] = '0' * 64
        request.write_text(json.dumps(altered))
        result = subprocess.run([str(adapter), '--verify-request', str(request), str(self.public)], capture_output=True)
        self.assertEqual(result.returncode, 4, 'production RSA verifier accepted changed hash')

    def test_private_file_and_no_overwrite(self):
        os.chmod(self.key, 0o644)
        try:
            self.run_tool('sign', '--image', self.image, '--version', '0.1.8-DRAFT',
                '--url', 'https://updates.example.test/image.bin', '--key', self.key, '--output', self.directory/'bad-key.json', ok=False)
        finally:
            os.chmod(self.key, 0o600)
        target = self.directory / 'read.json'
        self.run_tool('request', '--operation', 'read', '--telemetry', self.telemetry, '--output', target)
        self.run_tool('request', '--operation', 'read', '--telemetry', self.telemetry, '--output', target, ok=False)

    def test_invalid_version_HTTP_and_sequence(self):
        for suffix, version, url in [('version', '0.1.7', 'https://updates.example.test/image.bin'),
                                     ('http', '0.1.8-DRAFT', 'http://updates.example.test/image.bin')]:
            self.run_tool('sign', '--image', self.image, '--version', version, '--url', url,
                          '--key', self.key, '--output', self.directory/(suffix+'.json'), ok=False)
        self.run_tool('request', '--operation', 'stop', '--telemetry', self.telemetry,
                      '--sequence', '0', '--output', self.directory/'bad-seq.json', ok=False)

    def test_USB_import_is_owner_only_and_does_not_echo(self):
        config = dict(ssid='offline-fixture', wifi_password='TEST-NOT-A-DEPLOYED-CREDENTIAL',
                      broker='mqtts://example.test', username='fixture', password='TEST-NOT-A-DEPLOYED-CREDENTIAL',
                      ca='TEST-CA', update_public_key=self.public.read_text())
        source = self.directory/'config.json'
        fd = os.open(source, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(fd, 'w') as stream:
            json.dump(config, stream)
        target = self.directory/'usb-command.txt'
        result = self.run_tool('usb-import', '--config', source, '--output', target)
        self.assertNotIn(config['password'], result.stdout + result.stderr)
        self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)
        command = target.read_text(); self.assertTrue(command.startswith('network import '))
        self.assertEqual(json.loads(base64.b64decode(command.split()[2])), config)


if __name__ == '__main__':
    unittest.main(verbosity=2)
