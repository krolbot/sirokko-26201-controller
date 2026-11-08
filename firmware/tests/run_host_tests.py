#!/usr/bin/env python3
"""Compile and execute all portable production-code suites (no heater hardware)."""
from pathlib import Path
import subprocess
import sys
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sanitize", action="store_true", help="ASan/UBSan on every C++ suite")
options = parser.parse_args()

root = Path(__file__).resolve().parents[1]
build = root / ("build-host-sanitized" if options.sanitize else "build-host")
build.mkdir(exist_ok=True)
common = ["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
          "-I", str(root / "components/control_core/include"), "-I", str(root / "main")]
if options.sanitize:
    common += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
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
    "test_panel_canvas": [root / "main/panel_canvas.cpp"],
    "test_panel_render": [root / "main/panel_ui.cpp", root / "main/panel_canvas.cpp", root / "main/panel_render.cpp"],
    "test_panel_driver": [root / "main/panel_ui.cpp", root / "main/panel_canvas.cpp", root / "main/panel_render.cpp"],
    "test_ota_activation": [],
    "test_persistence": [],
    "test_sensor_driver": core + [root / "main/sensors.cpp"],
    "test_runtime_driver": core + [root / "main/start_stop_button.cpp"],
}
for name, sources in suites.items():
    binary = build / name
    stubs = {"test_panel_driver": "panel_stubs", "test_sensor_driver": "sensor_stubs",
             "test_runtime_driver": "runtime_host_stubs"}
    extra = ["-I", str(root / "tests" / stubs[name])] if name in stubs else []
    if name in ("test_ota_activation", "test_persistence"):
        extra += ["-pthread"]
    subprocess.run(common + extra + [str(p) for p in sources] +
                   [str(root / f"tests/{name}.cpp"), "-o", str(binary)], check=True)
    args = [str(binary)]
    if name == "test_control":
        args += [str(root / "tests/fixtures" / f) for f in
                 ("MAX31856_TEMPERATURE_VECTORS.csv", "DS18B20_SCRATCHPAD_VECTORS.csv")]
    subprocess.run(args, check=True)
subprocess.run([sys.executable, str(root / "tests/test_web_ota_host.py"), *(["--sanitize"] if options.sanitize else [])], check=True)
print(f"PASS: {len(suites)} control/panel suites plus complete web/OTA suite")
