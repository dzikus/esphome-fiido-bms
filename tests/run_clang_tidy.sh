#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

esphome_dependent=(
  fiido_bms.cpp
  fiido_gear_select.cpp
  fiido_mode_select.cpp
  fiido_speed_limit_select.cpp
  fiido_speed_unit_select.cpp
)

sources=()
for path in "${repo_dir}"/components/fiido_bms/*.cpp; do
  name="$(basename "${path}")"
  skip=0
  for excluded in "${esphome_dependent[@]}"; do
    if [[ "${name}" == "${excluded}" ]]; then
      skip=1
      break
    fi
  done
  if [[ "${skip}" -eq 0 ]]; then
    sources+=("${path}")
  fi
done

echo "clang-tidy over ${#sources[@]} sources"
clang-tidy --quiet "${sources[@]}" -- -std=gnu++20 -I "${repo_dir}/components/fiido_bms"
