#!/usr/bin/env python3
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
binary = root / "build-host" / "test_control"
binary.parent.mkdir(exist_ok=True)
subprocess.run([
    "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "components/control_core/include"),
    str(root / "components/control_core/control.cpp"),
    str(root / "tests/test_control.cpp"),
    "-o", str(binary),
], check=True)
subprocess.run([
    str(binary),
    str(root / "tests/fixtures/MAX31856_TEMPERATURE_VECTORS.csv"),
    str(root / "tests/fixtures/DS18B20_SCRATCHPAD_VECTORS.csv"),
], check=True)
