#!/usr/bin/env bash
set -euo pipefail

probe_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd "$probe_dir/../../.." && pwd)
output_dir="$project_root/reports/portability/3ds-native-expression"
toolchain_root="${DEVKITPRO:-/opt/devkitpro}"
toolchain_arm_dir="${DEVKITARM:-$toolchain_root/devkitARM}"
host_cxx="${CXX:-g++}"
arm_cxx="$toolchain_arm_dir/bin/arm-none-eabi-g++"
mkdir -p "$output_dir"

"$host_cxx" -std=c++17 -O2 -Wall -Wextra -Werror -DODH_SIMULATION_STATIC \
    -I"$project_root/native/simulation/include" \
    -I"$project_root/native/simulation/tests" \
    "$probe_dir/probe.cpp" "$project_root/native/simulation/src/odh_expression.cpp" \
    -o "$output_dir/host-probe"
"$output_dir/host-probe"

"$arm_cxx" -std=c++17 -O2 -Wall -Wextra -Werror -D__3DS__ -DODH_SIMULATION_STATIC \
    -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
    -mword-relocations -ffunction-sections -fdata-sections -specs=3dsx.specs \
    -I"$project_root/native/simulation/include" \
    -I"$project_root/native/simulation/tests" -I"$toolchain_root/libctru/include" \
    "$probe_dir/probe.cpp" "$project_root/native/simulation/src/odh_expression.cpp" \
    -L"$toolchain_root/libctru/lib" -lctru -lm \
    -Wl,-Map="$output_dir/odh-expression-probe.map" \
    -o "$output_dir/odh-expression-probe.elf"
"$toolchain_root/tools/bin/3dsxtool" \
    "$output_dir/odh-expression-probe.elf" "$output_dir/odh-expression-probe.3dsx"

file "$output_dir/odh-expression-probe.elf" "$output_dir/odh-expression-probe.3dsx"
