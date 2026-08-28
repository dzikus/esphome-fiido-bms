#!/usr/bin/env bash
set -euo pipefail

# ESP-IDF passes -Wall -Wextra -Werror and then cancels it with a trailing
# -Wno-error. Warnings here are emitted and ignored.

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
config="${1:?usage: $0 <esphome-config.yaml> [board]}"
board="${2:-esp32dev}"
esphome_bin="${ESPHOME:-esphome}"
log="${repo_dir}/.build/esp32-warnings.log"

mkdir -p "$(dirname "${log}")"

# esphome copies a source into the build tree only when its content changed, so
# touching it does not force a rebuild. Drop the objects instead.
config_dir="$(cd "$(dirname "${config}")" && pwd)"
find "${config_dir}/.esphome/build" -path "*fiido_bms*" -name "*.obj" -delete 2>/dev/null || true

"${esphome_bin}" -s board "${board}" compile "${config}" > "${log}" 2>&1 || {
  echo "build failed; see ${log}" >&2
  tail -30 "${log}" >&2
  exit 1
}

built="$(grep -oE "Building CXX object [^ ]*components/fiido_bms/[a-z_]+\.cpp" "${log}" | wc -l)"
if [[ "${built}" -eq 0 ]]; then
  echo "no component translation unit was compiled; the verdict would be empty" >&2
  exit 1
fi

if grep -E "components/fiido_bms/.*(warning|error):" "${log}"; then
  echo "component warnings above; see ${log}" >&2
  exit 1
fi
echo "no component warnings (${built} translation units compiled)"
