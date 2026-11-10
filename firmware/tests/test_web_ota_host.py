#!/usr/bin/env python3
"""Run the isolated real host tests for the Sirokko web/OTA module.

This runner deliberately does not touch the shared ESP-IDF CMake project.  It compiles
production parser/verifier/bounded-receive code and includes the production web.cpp in
a host test translation unit with test-only ESP transport/runtime shims.
"""

from __future__ import annotations

import os
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from contextlib import contextmanager

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"
MAIN = FIRMWARE / "main"
TESTS = FIRMWARE / "tests"


def required(name: str) -> str:
    value = shutil.which(name)
    if not value:
        raise RuntimeError(f"required host tool is unavailable: {name}")
    return value


@contextmanager
def test_signing_environment(work: Path):
    # Synthetic fixtures must NEVER be signed with the release private key.
    private = work / "ephemeral-test-key.pem"
    public = work / "ota_public_key.pem"
    subprocess.run([required("openssl"), "genpkey", "-algorithm", "RSA", "-pkeyopt",
                    "rsa_keygen_bits:3072", "-out", os.fspath(private)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    private.chmod(0o600)
    subprocess.run([required("openssl"), "pkey", "-in", os.fspath(private), "-pubout",
                    "-out", os.fspath(public)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    previous = os.environ.get("SIROKKO_OTA_SIGNING_KEY")
    os.environ["SIROKKO_OTA_SIGNING_KEY"] = os.fspath(private)
    try:
        yield public
    finally:
        if previous is None:
            os.environ.pop("SIROKKO_OTA_SIGNING_KEY", None)
        else:
            os.environ["SIROKKO_OTA_SIGNING_KEY"] = previous
        private.unlink(missing_ok=True)


def run(arguments: list[str]) -> None:
    print("+", " ".join(arguments), flush=True)
    # Node's assertion diagnostics must not consume the host's entire heap.
    # Keep the deadline outside JS so a synchronous loop cannot evade it.
    if Path(arguments[0]).name == "node":
        subprocess.run([arguments[0], "--max-old-space-size=128", *arguments[1:]],
                       cwd=ROOT, check=True, timeout=60)
    else:
        subprocess.run(arguments, cwd=ROOT, check=True)


def make_test_image(path: Path, chip_id: int) -> None:
    image = bytearray(index % 251 for index in range(4096))
    image[0] = 0xE9  # ESP_IMAGE_HEADER_MAGIC
    image[12:14] = chip_id.to_bytes(2, "little")  # esp_image_header_t::chip_id
    path.write_bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-dir", type=Path, help="export actual-handler page and explicit host fixture state")
    parser.add_argument("--sanitize", action="store_true", help="ASan/UBSan on production C++ parsers/handlers/verifier")
    args = parser.parse_args()
    try:
        cxx = required("g++")
        linker = required("ld")
        required("node")
    except RuntimeError as error:
        print(f"test_web_ota_host.py: {error}", file=sys.stderr)
        return 2

    scratch_base = Path(os.environ.get("TMPDIR", tempfile.gettempdir()))
    scratch_base.mkdir(parents=True, exist_ok=True)
    common = ["-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
              "-I", os.fspath(FIRMWARE / "components" / "control_core" / "include"),
              "-I", os.fspath(MAIN)]

    sanitizer = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"] if args.sanitize else []
    common += sanitizer
    try:
        with tempfile.TemporaryDirectory(prefix="sirokko-web-ota-", dir=scratch_base) as temporary, \
                test_signing_environment(Path(temporary)) as test_public_key:
            work = Path(temporary)
            valid_image = work / "valid.bin"
            wrong_chip_image = work / "wrong-chip.bin"
            generic_package = work / "generic.sota"
            valid_package = work / "valid.sota"
            wrong_chip_package = work / "wrong-chip.sota"
            make_test_image(valid_image, 0x0009)  # ESP_CHIP_ID_ESP32S3
            make_test_image(wrong_chip_image, 0x0000)

            # An ephemeral, test-only key is selected by the scoped environment above.
            run([sys.executable, os.fspath(FIRMWARE / "tools" / "sign_ota.py"),
                 "--input", os.fspath(valid_image), "--output", os.fspath(generic_package)])
            run([sys.executable, os.fspath(FIRMWARE / "tools" / "sign_ota.py"),
                 "--input", os.fspath(valid_image), "--output", os.fspath(valid_package)])
            run([sys.executable, os.fspath(FIRMWARE / "tools" / "sign_ota.py"),
                 "--input", os.fspath(wrong_chip_image), "--output", os.fspath(wrong_chip_package)])

            protocol = work / "test_web_protocol"
            run([cxx, *common, os.fspath(MAIN / "web_protocol.cpp"),
                 os.fspath(TESTS / "test_web_protocol.cpp"), "-o", os.fspath(protocol)])
            run([os.fspath(protocol)])

            verifier = work / "test_ota_verify"
            run([cxx, *common, os.fspath(MAIN / "web_protocol.cpp"), os.fspath(MAIN / "ota_verify.cpp"),
                 os.fspath(TESTS / "test_ota_verify.cpp"), "-lcrypto", "-o", os.fspath(verifier)])
            run([os.fspath(verifier), os.fspath(generic_package), os.fspath(test_public_key)])

            bounded = work / "test_web_bounded_receive"
            run([cxx, *common, os.fspath(MAIN / "bounded_receive.cpp"),
                 os.fspath(TESTS / "test_web_bounded_receive.cpp"), "-o", os.fspath(bounded)])
            run([os.fspath(bounded)])

            embedded_key = work / "ota_public_key_embed.o"
            # Embed the ephemeral public key in this test binary only.
            subprocess.run([linker, "-r", "-b", "binary", "-o", os.fspath(embedded_key),
                            "ota_public_key.pem"], cwd=work, check=True)

            handlers = work / "test_web_handlers"
            run([cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", *sanitizer,
                 "-I", os.fspath(TESTS / "web_host_stubs"), "-I", os.fspath(TESTS),
                 "-I", os.fspath(MAIN), "-I", os.fspath(FIRMWARE / "components" / "control_core" / "include"),
                 os.fspath(MAIN / "bounded_receive.cpp"), os.fspath(MAIN / "web_protocol.cpp"),
                 os.fspath(MAIN / "ota_verify.cpp"),
                 os.fspath(FIRMWARE / "components/control_core/profile.cpp"),
                 os.fspath(TESTS / "test_web_handlers.cpp"),
                 os.fspath(embedded_key), "-lcrypto", "-Wl,-z,noexecstack", "-o", os.fspath(handlers)])
            export = args.export_dir.resolve() if args.export_dir else work / "actual-handler"
            run([os.fspath(handlers), os.fspath(valid_package), os.fspath(wrong_chip_package), os.fspath(export)])
            run(["node", os.fspath(TESTS / "test_binary_contract.cjs")])
            run(["node", os.fspath(TESTS / "test_web_frontend.js"), os.fspath(export)])
            run(["node", "--test", *map(os.fspath, sorted((FIRMWARE / "web_ui/tests/unit").glob("*.test.js")))])
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired, RuntimeError) as error:
        print(f"test_web_ota_host.py: {error}", file=sys.stderr)
        return 1

    print("PASS: complete isolated Sirokko web/OTA host suite")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
