#!/usr/bin/env bash
# Adapted from PS5_EdgeOfTimeTitle/ps5/tools/link-title.sh.
# Copyright (C) 2026 Mihawk; original integration copyright (C) 2026 KongaTime.
# SPDX-License-Identifier: MIT
set -euo pipefail
build=$1 vulkan=$2 sdk=$3 runtime=$4
shift 4
objects=("$@")
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
tool="$vulkan/build/host/ps5-native-tool"
[[ -x $tool ]] || tool="$vulkan/build/runtime-shim/ps5-native-tool"
native="$vulkan/tooling/native"
for required in "$archive" "$tool" "$vulkan/runtime/libc.prx" "$runtime/librexruntime.a" "$runtime/librexgpu-xenos.a"; do
    [[ -f $required ]] || { echo "Required build artifact is missing: $required" >&2; exit 2; }
done
mkdir -p "$build/link/obj" "$build/link/stubs"
export PS5_CLANG=${PS5_CLANG:-$(command -v clang)}
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$build/link/obj/app_crt.o"
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$build/link/obj/${library}.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$build/link/stubs/${library}.so" "$build/link/obj/${library}.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive"
# Clang emits .ltext/.lrodata/.ldata with the large code model. Collect these
# alongside their regular sections, preserving the converter's segment layout.
radv_linker_script=(-T "$root/tools/wos-pie.ld")
bindings=()
printf '{\n local:\n' > "$build/link/wos-local.map"
for name in isatty pathconf realpath fork; do
    bindings+=("--defsym=$name=ps5_$name")
    printf '  %s;\n' "$name" >> "$build/link/wos-local.map"
done
printf '};\n' >> "$build/link/wos-local.map"
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --version-script "$build/link/wos-local.map" \
    --exclude-libs=ALL --wrap=abort --wrap=__imp__VdSwap --lto-O3 --thinlto-jobs=2 \
    --thinlto-cache-dir="$build/link/thinlto-cache" \
    -e _start -o "$build/link/llvm-pie.elf" \
    "$build/link/obj/app_crt.o" "${objects[@]}" \
    --start-group "$runtime"/*.a "$sdk/target/lib/libc++experimental.a" --end-group \
    "${bindings[@]}" "$build/link/stubs/libSceAgc.so" "$build/link/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so

# Refuse imports that resolve only to modules a normal title does not load.
null_imports=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$build/link/llvm-pie.elf" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$build/link/stubs/"*.so; do
        case ${library##*/} in libkernel_sys.so | libScePosixForWebKit.so) continue ;; esac
        "$sdk/bin/llvm-nm" -D --defined-only "$library" 2>/dev/null | awk '{ print $NF }'
      done | sort -u))
[[ -z $null_imports ]] || { printf 'Unresolved console imports:\n%s\n' "$null_imports" >&2; exit 1; }
"$tool" link --in "$build/link/llvm-pie.elf" --out "$build/eboot.elf.new" \
    --stub-dir "$sdk/target/lib" --stub "$build/link/stubs/libSceAgc.so" \
    --stub "$build/link/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
python3 "$root/tools/check-load-segments.py" "$build/eboot.elf.new"
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/sce_sys/param.json")
app="$root/dist/$title_id"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/import"
"$tool" self --sign --in "$build/eboot.elf.new" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin"
cp "$root/sce_sys/param.json" "$app/sce_sys/param.json"
[[ ! -f $root/sce_sys/icon0.png ]] || cp "$root/sce_sys/icon0.png" "$app/sce_sys/icon0.png"
(cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
rex_source=$(cd -- "$runtime/../.." && pwd)
python3 "$root/tools/stage-notices.py" "$app" "$(dirname "$vulkan")" "$archive" "$rex_source" "$sdk"
mv "$build/eboot.elf.new" "$build/eboot.elf"
printf 'Built development title: %s\n' "$app"
