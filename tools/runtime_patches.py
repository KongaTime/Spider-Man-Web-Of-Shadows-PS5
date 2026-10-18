"""Verify runtime patch chains against their pinned source, including overlaps."""
from pathlib import Path
import subprocess
import tempfile


def verified_runtime_patches(checkout, patch_dir):
    checkout, patch_dir = Path(checkout), Path(patch_dir)
    groups = [
        (['src/system/xthread.cpp'], [[]]),
        (['src/graphics/shared_memory.cpp'], [
            ['rexglue-single-range.patch', 'rexglue-valid-page-fast-path.patch']]),
        (['src/ui/imgui_drawer.cpp', 'src/ui/overlay/settings_overlay.cpp',
          'src/ui/rex_app.cpp'], [['rexglue-ps5-settings-navigation.patch']]),
        (['src/graphics/command_processor.cpp', 'src/graphics/vulkan/command_processor.cpp'], [
            ['rexglue-frame-diagnostics.patch', 'rexglue-disable-phase-timing.patch',
             'rexglue-coarse-frame-costs.patch', 'rexglue-texture-descriptor-reuse.patch',
             'rexglue-command-wait-breakdown.patch', 'rexglue-command-ring-wait.patch',
             'rexglue-sampled-draw-costs.patch'],
            ['rexglue-frame-diagnostics.patch', 'rexglue-disable-phase-timing.patch',
             'rexglue-coarse-frame-costs.patch', 'rexglue-texture-descriptor-reuse.patch',
             'rexglue-command-wait-breakdown.patch', 'rexglue-command-ring-wait.patch'],
            ['rexglue-frame-diagnostics.patch', 'rexglue-disable-phase-timing.patch',
             'rexglue-coarse-frame-costs.patch', 'rexglue-texture-descriptor-reuse.patch',
             'rexglue-command-wait-breakdown.patch'],
            ['rexglue-frame-diagnostics.patch', 'rexglue-disable-phase-timing.patch',
             'rexglue-coarse-frame-costs.patch', 'rexglue-texture-descriptor-reuse.patch'],
            ['rexglue-frame-diagnostics.patch', 'rexglue-disable-phase-timing.patch',
             'rexglue-coarse-frame-costs.patch'],
            ['rexglue-frame-diagnostics.patch', 'rexglue-disable-phase-timing.patch'],
            ['rexglue-frame-diagnostics.patch'], []]),
    ]
    verified = []
    for paths, candidates in groups:
        original = {p: subprocess.check_output(
            ['git', '-C', str(checkout), 'show', 'HEAD:' + p]) for p in paths}
        actual = {p: (checkout / p).read_bytes().replace(b'\r\n', b'\n') for p in paths}
        for names in candidates:
            with tempfile.TemporaryDirectory(prefix='wos-patch-check-') as scratch:
                scratch = Path(scratch)
                for p, data in original.items():
                    (scratch / p).parent.mkdir(parents=True, exist_ok=True)
                    (scratch / p).write_bytes(data)
                for name in names:
                    result = subprocess.run(
                        ['git', '-C', str(scratch), 'apply', str((patch_dir / name).resolve())],
                        capture_output=True)
                    if result.returncode:
                        raise RuntimeError(f'Patch cannot apply to pinned chain: {name}: '
                                           f'{result.stderr.decode(errors="replace")}')
                if all((scratch / p).read_bytes().replace(b'\r\n', b'\n') == actual[p]
                       for p in paths):
                    verified.extend(patch_dir / name for name in names)
                    break
        else:
            raise RuntimeError('Runtime source differs from recorded patches: ' + ', '.join(paths))
    return verified
