#!/usr/bin/env python3
"""Compile and execute all portable production-code suites (no heater hardware)."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = root / "build-host"
build.mkdir(exist_ok=True)
common = ["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
          "-I", str(root / "components/control_core/include"), "-I", str(root / "main")]
core = [root / "components/control_core" / name for name in
        ("control.cpp", "profile.cpp", "regulator.cpp", "calibration.cpp", "calibration_runtime.cpp")]
suites = {
    "test_control": core + [root / "main/bounded_receive.cpp"],
    "test_regulation_timer": core,
    "test_record_codec": core,
    "test_calibration": [root / "components/control_core/calibration.cpp"],
    "test_calibration_runtime": core,
    "test_start_stop_button": [root / "main/start_stop_button.cpp"],
    "test_panel_ui": [root / "main/panel_ui.cpp"],
    "test_panel_font": [root / "main/panel_font.cpp"],
    "test_ota_activation": [],
}
for name, sources in suites.items():
    binary = build / name
    subprocess.run(common + [str(p) for p in sources] +
                   [str(root / f"tests/{name}.cpp"), "-o", str(binary)], check=True)
    args = [str(binary)]
    if name == "test_control":
        args += [str(root / "tests/fixtures" / f) for f in
                 ("MAX31856_TEMPERATURE_VECTORS.csv", "DS18B20_SCRATCHPAD_VECTORS.csv")]
    subprocess.run(args, check=True)
subprocess.run([sys.executable, str(root / "tests/test_web_ota_host.py")], check=True)
print(f"PASS: {len(suites)} control/panel suites plus complete web/OTA suite")
