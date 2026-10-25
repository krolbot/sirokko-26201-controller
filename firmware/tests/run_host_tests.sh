#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
flags=(-std=c++20 -Wall -Wextra -Werror -pedantic -O2 -Icomponents/control_core/include -Imain -Itests -Itests/stubs)
common=(components/control_core/control.cpp components/control_core/button.cpp components/control_core/operator.cpp
  components/control_core/service.cpp components/control_core/ui.cpp main/output_driver.cpp main/commissioning.cpp tests/idf_stubs.cpp)
for suite in sim feature runtime display four_button; do
  g++ "${flags[@]}" "${common[@]}" "tests/${suite}_tests.cpp" -o "build-host/${suite}_tests"
  "build-host/${suite}_tests"
done
g++ "${flags[@]}" components/control_core/ui.cpp components/control_core/operator.cpp tests/ui_tests.cpp -o build-host/ui_tests
build-host/ui_tests
g++ "${flags[@]}" "${common[@]}" tests/render_ui.cpp -o build-host/render_ui
build-host/render_ui build-host/ui
if [[ "${1:-}" == "--core-only" ]]; then
  printf '%s\n' 'CORE-ONLY: network suite deliberately NOT run.'
  exit 0
fi
cjson="${SIROKKO_CJSON_DIR:-${IDF_PATH:-}/components/json/cJSON}"
if [[ ! -f "$cjson/cJSON.c" || ! -f "$cjson/cJSON.h" ]]; then
  printf '%s\n' 'Full network suite requires IDF 5.5.2 cJSON sources: set SIROKKO_CJSON_DIR (or IDF_PATH). OpenSSL development headers and libcrypto are also required.' >&2
  exit 2
fi
cc -O2 -I"$cjson" -c "$cjson/cJSON.c" -o build-host/cJSON.o
g++ "${flags[@]}" -Wno-unused-variable -I"$cjson" tests/network_tests.cpp build-host/cJSON.o \
  components/control_core/service.cpp components/control_core/control.cpp -lcrypto -o build-host/network_tests
build-host/network_tests
python3 tests/service_artifacts_tests.py
