#!/usr/bin/env python3
"""Offline service artifacts only: never connects, starts a heater, or prints credentials."""
import argparse
import base64
import hashlib
import json
import os
import re
import stat
import subprocess
from pathlib import Path
from urllib.parse import urlsplit

BOARD = 'ESP32-S3-DevKitC-1-N8R8-v1.1'
SCHEMA = 3


def version(value: str) -> tuple[int, int, int]:
    if not re.fullmatch(r'\d{1,2}\.\d{1,2}\.\d{1,2}-DRAFT', value):
        raise ValueError('expected NN.NN.NN-DRAFT')
    return tuple(map(int, value.removesuffix('-DRAFT').split('.')))


def canonical(manifest: dict) -> bytes:
    version(manifest['version'])
    url = urlsplit(manifest['url'])
    if (manifest['target'] != 'esp32s3' or manifest['board'] != BOARD or manifest['schema'] != SCHEMA
            or url.scheme != 'https' or not url.hostname or url.username or url.password
            or '\n' in manifest['url'] or '\r' in manifest['url'] or len(manifest['url'].encode()) >= 256
            or not re.fullmatch(r'[0-9a-f]{64}', manifest['sha256'])
            or type(manifest['size']) is not int or not 1024 <= manifest['size'] <= 0x300000):
        raise ValueError('invalid target/schema/HTTPS/image metadata')
    fields = ('SIROKKO-OTA-1', 'esp32s3', BOARD, str(SCHEMA), manifest['version'],
              str(manifest['size']), manifest['sha256'], manifest['url'], '')
    return '\n'.join(fields).encode('utf-8')


def private_file(path: Path) -> None:
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077:
        raise ValueError('secret input must be an owner-only regular file (0600)')


def save(path: Path, content: str, secret: bool = False) -> None:
    # Do not silently overwrite an existing key/config/request artifact.
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600 if secret else 0o644)
    with os.fdopen(descriptor, 'w') as stream:
        stream.write(content)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    sign = commands.add_parser('sign', help='sign an image manifest with an owner-held RSA-3072 key')
    sign.add_argument('--image', type=Path, required=True)
    sign.add_argument('--version', required=True)
    sign.add_argument('--url', required=True)
    sign.add_argument('--key', type=Path, required=True)
    sign.add_argument('--output', type=Path, required=True)
    request = commands.add_parser('request', help='prepare read/stop/update JSON; publish separately, never retained')
    request.add_argument('--operation', choices=('read', 'stop', 'update'), required=True)
    request.add_argument('--telemetry', type=Path, required=True)
    request.add_argument('--manifest', type=Path)
    request.add_argument('--sequence', type=int)
    request.add_argument('--output', type=Path, required=True)
    usb = commands.add_parser('usb-import', help='prepare a protected USB command file, no console echo')
    usb.add_argument('--config', type=Path, required=True)
    usb.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'sign':
        private_file(args.key)
        image = args.image.read_bytes()
        manifest = dict(target='esp32s3', board=BOARD, schema=SCHEMA, version=args.version,
                        size=len(image), sha256=hashlib.sha256(image).hexdigest(), url=args.url)
        signed = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(args.key)],
                                input=canonical(manifest), stdout=subprocess.PIPE, check=True)
        if len(signed.stdout) != 384:
            raise ValueError('signing key must be RSA-3072 (384-byte signature)')
        manifest['signature'] = base64.b64encode(signed.stdout).decode()
        save(args.output, json.dumps(manifest, separators=(',', ':')) + '\n')
    elif args.command == 'request':
        telemetry = json.loads(args.telemetry.read_text())
        if telemetry['board'] != BOARD or not re.fullmatch(r'[0-9a-f]{32}', telemetry['nonce']):
            raise ValueError('wrong board or missing fresh connection challenge')
        sequence = args.sequence if args.sequence is not None else int(telemetry['last_sequence']) + 1
        if not 1 <= sequence <= 0xffffffff:
            raise ValueError('sequence exhausted: reconnect the authenticated MQTT session')
        packet = dict(op=args.operation, nonce=telemetry['nonce'], seq=sequence,
                      uptime_ms=int(telemetry['uptime_ms']), ttl_ms=30000)
        if not 0 <= packet['uptime_ms'] <= 0xffffffff:
            raise ValueError('invalid telemetry uptime')
        if args.operation == 'update':
            if not args.manifest:
                raise ValueError('--manifest required for update')
            manifest = json.loads(args.manifest.read_text())
            canonical(manifest)
            if version(manifest['version']) <= version(telemetry['version']):
                raise ValueError('update must be newer than the running firmware')
            if len(base64.b64decode(manifest['signature'], validate=True)) != 384:
                raise ValueError('invalid signature length')
            packet.update({key: manifest[key] for key in ('target', 'board', 'schema', 'version', 'size', 'sha256', 'url', 'signature')})
        encoded = json.dumps(packet, separators=(',', ':'))
        if len(encoded.encode()) >= 2048:
            raise ValueError('request exceeds device packet bound')
        save(args.output, encoded + '\n')
    else:
        private_file(args.config)
        config = json.loads(args.config.read_text())
        limits = dict(ssid=33, wifi_password=65, broker=192, username=65, password=129, ca=4096, update_public_key=1024)
        if set(config) != set(limits) or any(not isinstance(config[k], str) or '\0' in config[k]
                                            or len(config[k].encode()) >= limit for k, limit in limits.items()):
            raise ValueError('invalid network config fields/lengths')
        if not config['broker'].startswith('mqtts://') or '@' in config['broker']:
            raise ValueError('TLS MQTT URI required; credentials must not be in URI')
        encoded = base64.b64encode(json.dumps(config, ensure_ascii=True, separators=(',', ':')).encode()).decode()
        if len(encoded) > 16000:
            raise ValueError('USB config exceeds import bound')
        save(args.output, 'network import ' + encoded + '\n', secret=True)
    print('Created ' + str(args.output) + '; no connection or heater command was sent.')


if __name__ == '__main__':
    main()
