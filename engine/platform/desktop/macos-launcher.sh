#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
set -u
odh_launcher_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)" || exit 1
odh_game_dir="$(cd -- "$odh_launcher_dir/.." && pwd -P)" || exit 1
odh_save_root="${ODH_SAVE_ROOT:-$HOME/Library/Application Support/opendollhouse/saves}"
odh_cache_root="${ODH_CACHE_ROOT:-$HOME/Library/Caches/opendollhouse}"
mkdir -p "$odh_save_root" "$odh_cache_root" || exit 1
cd -- "$odh_launcher_dir" || exit 1
printf 'OpenDollhouse — experimental macOS build\nGame data: %s\nLog: %s/launcher.log\n' "$odh_game_dir" "$odh_cache_root"
"$odh_launcher_dir/bin/opendollhouse" --data-root "$odh_game_dir" \
  --save-root "$odh_save_root" --cache-dir "$odh_cache_root" "$@" \
  2>&1 | tee "$odh_cache_root/launcher.log"
odh_game_status=${PIPESTATUS[0]}
if [ "$odh_game_status" -ne 0 ]; then
  printf '\nThe game exited with error %s. Include this log in your report: %s/launcher.log\n' \
    "$odh_game_status" "$odh_cache_root"
  if [ -t 0 ]; then read -r -p 'Press Enter to close.'; fi
fi
exit "$odh_game_status"
