#!/usr/bin/env bash
set -euo pipefail

# Runs clang-tidy over every component source, using the compile database of a
# real ESP32 build. Run it from the tree holding the CI config:
#
#   CI_CONFIG=.github/ci-build.yaml tests/run_clang_tidy.sh

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Staging keeps the two-hub CI config; dev only has the IntelliSense one.
ci_config="${CI_CONFIG:-}"
if [[ -z "${ci_config}" ]]; then
  for candidate in .github/ci-build.yaml .intellisense.yaml; do
    if [[ -f "${repo_dir}/${candidate}" ]]; then
      ci_config="${candidate}"
      break
    fi
  done
fi
board="${BOARD:-esp32-c3-devkitm-1}"
esphome_bin="${ESPHOME:-esphome}"
# Outside the repo, out of reach of git add.
work="$(mktemp -d -t clang-tidy-XXXXXX)"

if [[ ! -f "${repo_dir}/${ci_config}" ]]; then
  echo "no CI config at ${ci_config}; set CI_CONFIG" >&2
  exit 1
fi

# riscv, not xtensa: upstream clang has a riscv32 target and no xtensa one.
"${esphome_bin}" -s board "${board}" compile "${repo_dir}/${ci_config}"

config_dir="$(dirname "${repo_dir}/${ci_config}")"
# The tree can hold several build dirs; take the one this run just wrote.
# Other configs share .esphome/build; the newest database there can be for a
# different board. Go by the name esphome builds under, not by mtime.
name="$(sed -n '/^esphome:/,/^[a-z]/p' "${repo_dir}/${ci_config}" | sed -n 's/^  name: *//p' | head -1)"
if [[ -z "${name}" ]]; then
  echo "cannot read esphome.name from ${ci_config}" >&2
  exit 1
fi
db="${config_dir}/.esphome/build/${name}/build/compile_commands.json"
if [[ ! -f "${db}" ]]; then
  echo "no compile database at ${db}" >&2
  exit 1
fi

python3 - "${db}" "${work}" <<'PY'
import json, shlex, subprocess, sys

db_path, work = sys.argv[1], sys.argv[2]
entries = [e for e in json.load(open(db_path)) if '/components/fiido_bms/' in e['file']]
if not entries:
    sys.exit('compile database holds no fiido_bms translation unit')

# clang does not carry the cross toolchain's own search paths; gcc reports them.
gxx = shlex.split(entries[0]['command'])[0]
probe = subprocess.run([gxx, '-E', '-x', 'c++', '-', '-v'], input='', capture_output=True, text=True).stderr
system_includes, inside = [], False
for line in probe.splitlines():
    if line.startswith('#include <...>'):
        inside = True
        continue
    if line.startswith('End of search list'):
        break
    if inside:
        system_includes.append(line.strip())

gcc_only = {'-fstrict-volatile-bitfields', '-fno-tree-switch-conversion', '-freorder-blocks', '-mlongcalls'}
out = []
for e in entries:
    args = [a for a in shlex.split(e['command']) if a not in gcc_only]
    args[0] = 'clang++'
    extra = ['--target=riscv32-unknown-elf', '-nostdinc++']
    for path in system_includes:
        extra += ['-isystem', path]
    args[1:1] = extra
    out.append({'directory': e['directory'], 'file': e['file'],
                'command': ' '.join(shlex.quote(a) for a in args)})

json.dump(out, open(work + '/compile_commands.json', 'w'), indent=1)
print('\n'.join(e['file'] for e in out))
PY

mapfile -t sources < <(python3 -c "
import json, sys
print('\n'.join(e['file'] for e in json.load(open('${work}/compile_commands.json'))))")

echo "clang-tidy over ${#sources[@]} sources"
clang-tidy -p "${work}" --quiet --header-filter='.*/components/fiido_bms/.*' "${sources[@]}"
