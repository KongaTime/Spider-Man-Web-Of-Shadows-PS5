"""Stage notices for the components actually linked by this development title."""
import json
import os
import pathlib
import re
import shutil
import sys
from release_licenses import copy_notices, dependency_part, source_info
from runtime_patches import verified_runtime_patches

root = pathlib.Path(__file__).resolve().parent.parent
app, dependencies, archive, rex_source, sdk = map(pathlib.Path, sys.argv[1:6])
output = app / 'licenses'
output.mkdir(parents=True, exist_ok=True)
parts = []
for ident, checkout, label in [
    ('rexglue', rex_source, 'ReXGlue runtime, Xenos backend, and initialized dependencies'),
    ('ps5-vulkan', dependencies / 'PS5_Vulkan', 'Native CRT, link recipe, converter and clean-room libc.prx'),
]:
    part = dependency_part(checkout, ident, label,
                           'See the accompanying component license texts', [], output)
    if ident == 'rexglue':
        import hashlib
        part['source']['local_patches'] = []
        # Replay overlapping patches from HEAD and compare the resulting source.
        # Reverse-checking each patch independently cannot verify such chains.
        for patch in verified_runtime_patches(checkout, root / 'patches'):
            shutil.copyfile(patch, output / patch.name)
            part['source']['local_patches'].append(dict(
                file='licenses/' + patch.name,
                sha256=hashlib.sha256(patch.read_bytes()).hexdigest()))
    parts.append(part)

payload_source = pathlib.Path(os.environ.get('PS5_PAYLOAD_SDK_FORK', dependencies / 'PS5_PayloadSDK'))
payload_revision = (sdk / '.ps5-sdk-revision').read_text().strip()
copy_notices(payload_source, payload_revision, output / 'platform')
payload_info = source_info(payload_source, payload_revision)
payload_info['dirty'] = False  # The SDK setup exported the stamped commit.
parts.append(dict(id='platform', name='PS5 payload SDK and platform layer',
                  texts='licenses/platform/', source=payload_info))

provenance = (archive.parent.parent / 'PROVENANCE.txt').read_text()
mesa_revision = re.search(r'^revision: ([0-9a-f]{40})$', provenance, re.M).group(1)
mesa = pathlib.Path(re.search(r'^fork: (.+)$', provenance, re.M).group(1))
copy_notices(mesa, mesa_revision, output / 'radv')
mesa_source = source_info(mesa, mesa_revision)
mesa_source['dirty'] = False  # build-radv.sh exports this exact committed revision.
parts.append(dict(id='radv', name='Mesa RADV, ACO, NIR and Vulkan runtime',
                  texts='licenses/radv/', source=mesa_source))

zlib = dependencies / 'PS5_Vulkan/.deps/work/radv-src/subprojects/zlib-1.3.1'
zlib_output = output / 'zlib'
zlib_output.mkdir(exist_ok=True)
for notice in ('LICENSE', 'LICENSE.build'):
    shutil.copyfile(zlib / notice, zlib_output / notice)
parts.append(dict(id='zlib', name='zlib 1.3.1 and Meson build definitions',
                  texts='licenses/zlib/', source=dict(kind='archive',
                  url='https://zlib.net/fossils/zlib-1.3.1.tar.gz',
                  sha256='9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23',
                  build_definitions='https://wrapdb.mesonbuild.com/v2/zlib_1.3.1-1/get_patch',
                  build_sha256='e79b98eb24a75392009cec6f99ca5cdca9881ff20bfa174e8b8926d5c7a47095')))

for component, files in {
    'title': [root / 'LICENSE', root / 'vendor/platform/LICENSE.md', root / 'vendor/volk/LICENSE.md',
              root / 'vendor/rex-clock/LICENSE'],
    'llvm-runtime': [root / 'vendor/llvm-runtime/LICENSE.TXT'],
}.items():
    destination = output / component
    destination.mkdir(exist_ok=True)
    for index, path in enumerate(files):
        shutil.copyfile(path, destination / f'{index}-{path.name}')
    parts.append(dict(id=component, name=component, texts=f'licenses/{component}/',
                      source=dict(kind='fixed', revision='See dependencies.lock.json and source package')))

# This text records the provenance of the platform allocator, whose filename
# does not match the generic license-document name pattern.
import subprocess
allocator = subprocess.check_output(['git', '-C', str(payload_source), 'show',
    payload_revision + ':platform/src/dlmalloc/SOURCE'])
(output / 'platform/dlmalloc-SOURCE.txt').write_bytes(allocator)

def public_source(info):
    info.pop('path', None)
    for child in info.get('submodules', {}).values():
        public_source(child)
for part in parts:
    public_source(part['source'])
    destination = app / part['texts']
    if not destination.is_dir() or not any(p.is_file() for p in destination.rglob('*')):
        raise SystemExit(f'Missing notices for {part["id"]}')
(output / 'components.json').write_text(json.dumps(parts, indent=2) + '\n')
shutil.copyfile(root / 'dependencies.lock.json', app / 'dependencies.lock.json')
(app / 'LEGAL.txt').write_text(
    'Web of Shadows PS5 development build\n\n'
    'New integration sources are MIT licensed. The linked PS5 platform and CRT '
    'include GPL-3.0-or-later components; other dependencies retain their own terms. '
    'See licenses/components.json and the corresponding source repositories and revisions.\n'
    'Original game data is supplied by the player and is not part of this package. '
    'Spider-Man and Web of Shadows trademarks belong to their respective owners.\n'
    'This is an unofficial development build. Initial gameplay works, but '
    'performance and full-game validation remain incomplete.\n')
print('Staged notices for', len(parts), 'component groups')
