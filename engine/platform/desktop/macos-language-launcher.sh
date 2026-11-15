#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
odh_launcher_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)" || exit 1
exec "$odh_launcher_dir/OpenDollhouse.command" --choose-language "$@"
