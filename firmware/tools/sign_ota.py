#!/usr/bin/env python3
"""Create the single-file signed Sirokko OTA container offline.

The private RSA key is never copied into the repository or printed. The target
verifies the signature with firmware/main/ota_public_key.pem before selecting
an OTA boot partition.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile

MAGIC = b"SIROTA01"
FORMAT_VERSION = 1
PROJECT_FIELD_SIZE = 32
TARGET_FIELD_SIZE = 16
DIGEST_SIZE = 32
HEADER = struct.Struct(">8sIII32s16s32s")
SIGNATURE_SIZE = 384  # RSA-3072
DEFAULT_PROJECT = "sirokko_26201_controller"
DEFAULT_TARGET = "esp32s3"
DEFAULT_MAX_IMAGE_SIZE = 0x700000
SAFE_NAME = re.compile(r"^[A-Za-z0-9_-]+$")


def parse_size(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid size: {value}") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return parsed


def fixed_text(value: str, field_size: int, label: str) -> bytes:
    encoded = value.encode("ascii", "strict")
    if not SAFE_NAME.fullmatch(value) or len(encoded) >= field_size:
        raise ValueError(f"{label} must be ASCII [A-Za-z0-9_-] and fit its manifest field")
    return encoded + b"\0" * (field_size - len(encoded))


def secure_key_path(value: Path) -> Path:
    try:
        details = value.stat()
    except OSError as error:
        raise ValueError("signing key is unavailable") from error
    if not stat.S_ISREG(details.st_mode) or details.st_mode & 0o077:
        raise ValueError("signing key must be a regular owner-only file (0600)")
    return value


def sign_header(header: bytes, key_path: Path) -> bytes:
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("openssl is required for offline signing")
    with tempfile.TemporaryDirectory(prefix="sirokko-ota-") as work:
        header_path = Path(work) / "manifest.bin"
        signature_path = Path(work) / "manifest.sig"
        header_path.write_bytes(header)
        completed = subprocess.run(
            [openssl, "dgst", "-sha256", "-sign", os.fspath(key_path), "-out", os.fspath(signature_path), os.fspath(header_path)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if completed.returncode != 0 or not signature_path.is_file():
            raise RuntimeError("OpenSSL signature operation failed")
        signature = signature_path.read_bytes()
    if len(signature) != SIGNATURE_SIZE:
        raise RuntimeError("signing key is not RSA-3072")
    return signature


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a signed Sirokko OTA package without exposing the private key.")
    env_key = os.environ.get("SIROKKO_OTA_SIGNING_KEY")
    parser.add_argument("--input", required=True, type=Path, help="ESP-IDF application .bin")
    parser.add_argument("--output", required=True, type=Path, help="output .sota package")
    parser.add_argument("--project", default=DEFAULT_PROJECT, help="expected ESP-IDF project_name")
    parser.add_argument("--target", default=DEFAULT_TARGET, help="expected target tag")
    parser.add_argument("--max-image-size", type=parse_size, default=DEFAULT_MAX_IMAGE_SIZE,
                        help="maximum raw image size; default matches Sirokko OTA slot (0x700000)")
    parser.add_argument(
        "--key",
        type=Path,
        default=Path(env_key) if env_key else None,
        help="owner-only RSA-3072 private key path; required via --key or SIROKKO_OTA_SIGNING_KEY",
    )
    args = parser.parse_args()

    try:
        if args.key is None:
            raise ValueError("signing key must be specified with --key or SIROKKO_OTA_SIGNING_KEY")
        key_path = secure_key_path(args.key)
        image = args.input.read_bytes()
        if not image:
            raise ValueError("input image is empty")
        if len(image) > args.max_image_size:
            raise ValueError("input image exceeds configured OTA slot limit")
        project = fixed_text(args.project, PROJECT_FIELD_SIZE, "project")
        target = fixed_text(args.target, TARGET_FIELD_SIZE, "target")
        image_digest = hashlib.sha256(image).digest()
        header = HEADER.pack(MAGIC, FORMAT_VERSION, HEADER.size, len(image), project, target, image_digest)
        if len(header) != HEADER.size or HEADER.size != 100:
            raise RuntimeError("internal manifest size mismatch")
        signature = sign_header(header, key_path)
        output = args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix=".sirokko-ota-", dir=output.parent, delete=False) as temporary:
            temporary.write(header)
            temporary.write(image)
            temporary.write(signature)
            temporary.flush()
            os.fsync(temporary.fileno())
            temporary_path = Path(temporary.name)
        os.chmod(temporary_path, 0o644)
        os.replace(temporary_path, output)
    except (OSError, RuntimeError, ValueError, UnicodeError) as error:
        print(f"sign_ota.py: {error}", file=sys.stderr)
        return 2

    print(f"signed OTA package written: {args.output} (image bytes: {len(image)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
