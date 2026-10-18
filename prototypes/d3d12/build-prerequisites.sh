#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Follows PS5_Vulkan's build-radv-title.sh (Mihawk-99) with a separate title.
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
workspace=${PS5_D3D12_WORKSPACE:-$(cd "$project/../.." && pwd)}
root=${PS5_VULKAN_ROOT:-$workspace/work/deps/PS5_Vulkan}
sdk_root=${PS5_PAYLOAD_SDK:-$workspace/work/deps/PS5_EdgeOfTimeTitle/.deps/native/ps5-payload-sdk}
archive="$root/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
native="$root/tooling/native"
work=${PS5_D3D12_BUILD_DIR:-$workspace/work/wos/d3d12-prerequisites}
source_file=${D3D12_PROBE_SOURCE:-prerequisites.c}
vkd3d_build=${VKD3D_BUILD:-$workspace/work/wos/vkd3d-ps5/build}
vkd3d_root=${VKD3D_ROOT:-$workspace/work/deps/vkd3d-proton-probe}
extra_inputs=()
if [[ $source_file == device-probe.c ]]; then
    mkdir -p "$work/obj" "$work/gen"
    python3 "$project/prototypes/d3d12/extract-rov-shaders.py" \
        "$vkd3d_root/tests/d3d12_rov.c" "$work/gen/rov-shaders.h"
    for part in debug main; do
    PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" -std=c11 -O2 -ffunction-sections -fdata-sections \
        -I "$vkd3d_root/include" -I "$vkd3d_root/include/private" \
        -I "$vkd3d_build/libs/vkd3d-common/libvkd3d_common.a.p" \
        -I "$vkd3d_root/khronos/Vulkan-Headers/include" \
        -c "$vkd3d_root/libs/d3d12core/$part.c" -o "$work/obj/d3d12-$part.o"
    done
    extra_inputs=(--start-group "$vkd3d_build/libs/vkd3d/libvkd3d-proton.a"
        "$vkd3d_build/libs/vkd3d-common/libvkd3d_common.a"
        "$vkd3d_build/libs/vkd3d-shader/libvkd3d-shader.a"
        "$vkd3d_build/subprojects/dxil-spirv/libdxil-spirv.a"
        "$vkd3d_build/subprojects/dxil-spirv/libdxbc_spv_module.a"
        "$vkd3d_build/subprojects/dxil-spirv/subprojects/dxbc-spirv/libdxbc_spv.a" \
        "$work/obj/d3d12-debug.o" "$work/obj/d3d12-main.o" --end-group)
fi
app="$work/dist/PPSA99302"
tool="$root/build/runtime-shim/ps5-native-tool"
mkdir -p "$work/obj" "$work/stubs" "$app/sce_sys" "$app/sce_module"
cc() { PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" "$@"; }
cc -std=c11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
    -I "$vkd3d_root/khronos/Vulkan-Headers/include" \
    -I "$vkd3d_root/include" -I "$vkd3d_build/libs/vkd3d-common/libvkd3d_common.a.p" -I "$work/gen" \
    -c "$project/prototypes/d3d12/$source_file" -o "$work/obj/probe.o"
for source in app_crt app_cpp_runtime; do
    cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
        -c "$native/$source.cpp" -o "$work/obj/$source.o"
done
stub() {
    cc -std=c11 -O2 -fPIC -c "$root/$2" -o "$work/obj/$1.o"
    "$sdk_root/bin/prospero-lld" --shared -soname "$1.prx" -o "$work/stubs/$1.so" "$work/obj/$1.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c
source "$root/tools/radv-link.sh"
radv_link_recipe "$root" "$sdk_root" "$archive"
"$sdk_root/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL -e _start -o "$work/llvm-pie.elf" \
    "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" "$work/obj/probe.o" \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" "${extra_inputs[@]}" "${radv_link_inputs[@]}" \
    --as-needed "$sdk_root"/target/lib/*.so
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" --stub-dir "$sdk_root/target/lib" \
    --stub "$work/stubs/libSceAgc.so" --stub "$work/stubs/libSceAgcDriver.so" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
python3 - "$project/sce_sys/param.json" "$app/sce_sys/param.json" <<'PY'
import json,sys
p=json.load(open(sys.argv[1])); p['titleId']='PPSA99302'; p['conceptId']='99302'
p['contentId']='UP9000-PPSA99302_00-WOSD3D12PROBE000'
p['localizedParameters']['en-US']['titleName']='WoS D3D12 Prerequisite Probe'
with open(sys.argv[2],'w') as f: json.dump(p,f,indent=2); f.write('\n')
PY
cp "$project/sce_sys/icon0.png" "$app/sce_sys/icon0.png"
cp "$root/runtime/libc.prx" "$app/sce_module/libc.prx"
"$tool" self --inspect --file "$app/eboot.bin" > "$work/self-inspect.txt"
printf 'Built isolated prerequisite probe: %s\n' "$app"
