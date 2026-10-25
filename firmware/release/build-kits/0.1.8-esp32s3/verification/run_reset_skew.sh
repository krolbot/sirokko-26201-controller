#!/usr/bin/env bash
set -euo pipefail
source_dir="$(cd "${1:?pass extracted firmware source directory}" && pwd)"
cd "$(dirname "$0")"
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 \
  -I"$source_dir/components/control_core/include" -I"$source_dir/main" -I"$source_dir/tests" -I"$source_dir/tests/stubs" \
  "$source_dir/components/control_core/control.cpp" "$source_dir/components/control_core/button.cpp" \
  "$source_dir/components/control_core/operator.cpp" "$source_dir/components/control_core/service.cpp" \
  "$source_dir/components/control_core/ui.cpp" "$source_dir/main/output_driver.cpp" \
  "$source_dir/main/commissioning.cpp" "$source_dir/tests/idf_stubs.cpp" reset_skew.cpp -o reset_skew
./reset_skew
rm reset_skew
