#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
workspace=${WOS_WORKSPACE:-$(cd "$root/../.." && pwd)}
[[ $# == 1 ]] || { echo "usage: bash tools/codegen.sh EXTRACTED_GAME_FOLDER" >&2; exit 2; }
game=$1
if [[ $game =~ ^[A-Za-z]: ]]; then game=$(wslpath -u "$game"); fi
game=$(cd -- "$game" && pwd)
[[ -f $game/default.xex ]] || { echo "default.xex is missing" >&2; exit 2; }
cli=${REXGLUE_CLI:-$workspace/work/tools/rexglue/linux-amd64/bin/rexglue}
[[ -x $cli ]] || { echo "Set REXGLUE_CLI to the pinned Linux ReXGlue executable" >&2; exit 2; }
work="$workspace/work/wos"
mkdir -p "$work"
python3 - "$root" "$work" "$game" <<'PY'
import hashlib, json, pathlib, sys
root, work, game = map(pathlib.Path, sys.argv[1:])
expected = '3183f0beab0df589137da4430de4ac6716d281fd732b7c6b71d153140add479c'
if hashlib.sha256((game / 'default.xex').read_bytes()).hexdigest() != expected:
    raise SystemExit('Unsupported default.xex revision; this configuration is for the USA retail executable')
quote = lambda value: json.dumps(str(value))
(work / 'wos_manifest.toml').write_text(
    '[project]\nname = "wos_recomp"\nsdk_version = "0.10.0"\n'
    'game_root = ' + quote(game) + '\n\n[entrypoint]\n'
    'file_path = ' + quote(game / 'default.xex') + '\n'
    'out_directory_path = ' + quote(work / 'generated/default') + '\n'
    'includes = [' + quote(root / 'config/wos_boundaries.toml') + ']\n')
PY
previous="$work/wos_recomp_pch.before-codegen.h"
pch="$work/generated/default/wos_recomp_pch.h"
[[ ! -f $pch ]] || cp -p -- "$pch" "$previous"
"$cli" codegen "$work/wos_manifest.toml"
# Keep the PS5 physical mapping condition in regenerated headers. If that is
# the only emitted difference, preserve its timestamp so unchanged translation
# units can keep their already-compiled precompiled header.
python3 - "$pch" "$previous" <<'PY'
import os, pathlib, sys
pch, previous = map(pathlib.Path, sys.argv[1:])
old = b'#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)'
new = old + b' || REX_PLATFORM_PS5'
data = pch.read_bytes()
if new not in data:
    if old not in data:
        raise SystemExit('Unrecognized generated physical-memory mapping condition')
    pch.write_bytes(data.replace(old, new))
if previous.exists() and pch.read_bytes() == previous.read_bytes():
    stamp = previous.stat()
    os.utime(pch, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
PY
python3 "$root/tools/check-generated-labels.py" "$work/generated/default"
