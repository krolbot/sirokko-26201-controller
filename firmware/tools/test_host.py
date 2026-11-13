#!/usr/bin/env python3
"""Run portable firmware tests without ESP-IDF or connected hardware."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
core = root / "components/control_core"
flags = [
    "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g",
    "-fno-omit-frame-pointer", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
    "-I" + str(core / "include"), "-I" + str(root / "main"),
]
core_sources = [str(core / name) for name in (
    "control.cpp", "profile.cpp", "regulator.cpp", "calibration.cpp", "calibration_runtime.cpp", "combustion.cpp",
)]
suites = {
    "combustion": core_sources,
    "control": core_sources,
    "recovery": core_sources,
    "calibration": [str(core / "calibration.cpp"), str(core / "regulator.cpp")],
    "runtime": core_sources + [str(root / "main" / name) for name in (
        "web_protocol.cpp", "web_state.cpp",
    )],
    "profile": core_sources + [str(root / "main" / name) for name in (
        "web_protocol.cpp", "web_state.cpp",
    )],
    "io": core_sources + [str(root / "main" / name) for name in (
        "web_protocol.cpp", "start_stop_button.cpp", "bounded_receive.cpp",
    )],
    "panel": core_sources + [str(root / "main" / name) for name in (
        "panel_ui.cpp", "panel_render.cpp", "panel_canvas.cpp",
    )],
    "ota": core_sources + [str(root / "main" / name) for name in (
        "ota_verify.cpp", "web_protocol.cpp",
    )] + ["-lcrypto"],
    "wifi_password": [],
    "captive_dns": [],
}
with tempfile.TemporaryDirectory(prefix="sirokko-tests-") as temporary:
    for name, sources in suites.items():
        binary = str(Path(temporary) / name)
        subprocess.run([
            os.environ.get("CXX", "g++"), *flags,
            str(root / "tests" / f"{name}_test.cpp"), *sources, "-o", binary,
        ], check=True)
        subprocess.run([binary], check=True)
print(f"{len(suites)} host suites passed (ASan + UBSan)")
