#!/usr/bin/env bash
set -euo pipefail
mkdir -p build-host
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 \
  -Icomponents/control_core/include -Imain \
  components/control_core/control.cpp components/control_core/button.cpp tests/sim_tests.cpp \
  -o build-host/sim_tests
exec ./build-host/sim_tests
