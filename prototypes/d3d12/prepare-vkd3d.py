#!/usr/bin/env python3
"""Configure the pinned native static vkd3d experiment; run inside WSL/Linux."""
import argparse, pathlib, shutil, subprocess
p=argparse.ArgumentParser()
p.add_argument('--source',type=pathlib.Path,required=True)
p.add_argument('--build',type=pathlib.Path,required=True)
p.add_argument('--sdk',type=pathlib.Path,required=True)
p.add_argument('--vulkan',type=pathlib.Path,required=True)
p.add_argument('--widl',type=pathlib.Path,required=True)
args=p.parse_args()
source=args.source.resolve(); build=args.build.resolve(); sdk=args.sdk.resolve(); vulkan=args.vulkan.resolve()
here=pathlib.Path(__file__).resolve().parent
pin='0bb64c56c8f0dcc11ecc0f6c56e00e4f7bd8d506'
def git(*arguments,**kwargs): return subprocess.run(['git','-C',str(source),*arguments],**kwargs)
head=git('rev-parse','HEAD',check=True,capture_output=True,text=True).stdout.strip()
if head != pin: raise SystemExit('Use the documented source pin, not an arbitrary version: '+pin)
patch=str(here/'vkd3d-proton-ps5.patch')
# Windows checkouts may have CRLF even though Git's patch uses LF. Normalize
# only the explicit patch targets; no source statements or user edits change.
for line in pathlib.Path(patch).read_text().splitlines():
    if line.startswith('+++ b/'):
        path=source/line[6:]
        path.write_bytes(path.read_bytes().replace(b'\r\n',b'\n'))
check=git('apply','--check',patch,capture_output=True)
if check.returncode == 0: git('apply',patch,check=True)
elif git('apply','--reverse','--check',patch,capture_output=True).returncode:
    raise SystemExit('Source differs from both the clean and patched pin. Preserve it and use a fresh checkout.')
shutil.copyfile(here/'vkd3d_ps5_sync.h',source/'include/private/vkd3d_ps5_sync.h')
build.mkdir(parents=True,exist_ok=True)
for language in ['c','cpp']:
    path=build/(language+'.sh')
    path.write_bytes((f'#!/bin/sh\nexport PS5_PAYLOAD_SDK="{sdk}"\nexport PATH="$PS5_PAYLOAD_SDK/bin:$PATH"\n'
        f'exec sh "{vulkan}/tooling/prospero-clang18" '+('-x c++ ' if language=='cpp' else '')+'"$@"\n').encode())
cross=build/'cross.ini'
cross.write_text(f'''[binaries]
c = ['sh', '{build}/c.sh']
cpp = ['sh', '{build}/cpp.sh']
ar = '{sdk}/bin/llvm-ar'
strip = 'llvm-strip'
widl = '{args.widl.resolve()}'
widl-mingw-tools-fallback = '{args.widl.resolve()}'
[host_machine]
system = 'freebsd'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
[properties]
needs_exe_wrapper = true
skip_sanity_check = true
''')
target=build/'build'
configure=['meson','setup',str(target),str(source),'--cross-file',str(cross),'--buildtype','release',
    '-Denable_tests=false','-Denable_extras=false']
if (target/'meson-private/coredata.dat').exists(): configure.append('--reconfigure')
subprocess.run(configure,check=True)
subprocess.run(['ninja','-C',str(target),'-j','6','libs/vkd3d/libvkd3d-proton.a',
    'libs/vkd3d-common/libvkd3d_common.a','libs/vkd3d-shader/libvkd3d-shader.a',
    'subprojects/dxil-spirv/libdxil-spirv.a','subprojects/dxil-spirv/libdxbc_spv_module.a',
    'subprojects/dxil-spirv/subprojects/dxbc-spirv/libdxbc_spv.a'],check=True)
