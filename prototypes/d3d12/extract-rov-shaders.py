#!/usr/bin/env python3
"""Extract two LGPL test shaders from the pinned vkd3d-proton source tree."""
import pathlib, re, sys
source = pathlib.Path(sys.argv[1]).read_text()
text = '/* SPDX-License-Identifier: LGPL-2.1-or-later\n * Copyright 2023 Hans-Kristian Arntzen for Valve Corporation.\n * From vkd3d-proton tests/d3d12_rov.c; generated, do not edit.\n */\n'
for name in ('vs_code_dxbc', 'ps_code_single_dxbc'):
    match = re.search(r'static const DWORD '+name+r'\[\]\s*=\s*\{.*?\};', source, re.S)
    if not match:
        raise SystemExit('Pinned upstream shader declaration not found: '+name)
    text += match.group(0)+'\n'
pathlib.Path(sys.argv[2]).write_bytes(text.encode())
