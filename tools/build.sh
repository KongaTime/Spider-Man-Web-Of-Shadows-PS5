#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
workspace=${WOS_WORKSPACE:-$(cd "$root/../.." && pwd)}
deps=${WOS_DEPS_DIR:-$workspace/work/deps}
sdk=${PS5_PAYLOAD_SDK:-$deps/PS5_EdgeOfTimeTitle/.deps/native/ps5-payload-sdk}
runtime_build=${REXGLUE_PS5_BUILD:-$deps/EdgeOfTimeRecomp/build-ps5/rexglue}
generated=${WOS_GENERATED_DIR:-$workspace/work/wos/generated/default}
build=${WOS_BUILD_DIR:-$workspace/work/wos/build-ps5}
python3 "$root/tools/check-generated-labels.py" "$generated"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF \
    -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
    -DPS5_PAYLOAD_SDK="$sdk" -DPS5_VULKAN_DIR="${PS5_VULKAN_DIR:-$deps/PS5_Vulkan}" \
    -DREXGLUE_SRC="${REXGLUE_SRC:-$deps/rexglue-sdk}" \
    -DREXGLUE_PS5_BUILD="$runtime_build" -DWOS_GENERATED_DIR="$generated"
cmake --build "$build" --parallel "${BUILD_JOBS:-2}" --target "${1:-title}"
