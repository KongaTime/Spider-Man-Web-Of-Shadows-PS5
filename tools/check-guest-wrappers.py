"""Verify the unstripped linked PPC mapping table actually references diagnostic wrappers."""
from pathlib import Path
import argparse
import re
import struct

parser = argparse.ArgumentParser()
parser.add_argument('elf', type=Path)
parser.add_argument('mapping_source', type=Path)
args = parser.parse_args()
data = args.elf.read_bytes()
if data[:6] != b'\x7fELF\x02\x01':
    raise SystemExit('Expected a little-endian ELF64 image')
header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
sections = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + i * header[11])
            for i in range(header[12])]
symbols = {}
symbol_tables = {}
for index, section in enumerate(sections):
    if section[1] not in (2, 11):
        continue
    strings = sections[section[6]]
    names = data[strings[4]:strings[4] + strings[5]]
    table = []
    for offset in range(section[4], section[4] + section[5], section[9]):
        symbol = struct.unpack_from('<IBBHQQ', data, offset)
        end = names.find(b'\0', symbol[0])
        if end == -1:
            raise SystemExit('Unterminated ELF symbol name')
        name = names[symbol[0]:end].decode(errors='replace')
        table.append(symbol)
        if symbol[3] and name:
            symbols[name] = symbol
    symbol_tables[index] = table
relocations = {}
for section in sections:
    if section[1] != 4:
        continue
    for offset in range(section[4], section[4] + section[5], section[9]):
        address, info, addend = struct.unpack_from('<QQq', data, offset)
        kind = info & 0xffffffff
        if kind == 8:  # R_X86_64_RELATIVE, linked at base zero.
            relocations[address] = addend
        elif kind in (1, 6):
            table = symbol_tables[section[6]]
            relocations[address] = table[info >> 32][4] + addend
mapping = symbols['PPCFuncMappings']
section = sections[mapping[3]]
offset = section[4] + mapping[4] - section[3]
entries = {}
for relative in range(0, mapping[5], 16):
    guest, host = struct.unpack_from('<QQ', data, offset + relative)
    entries[guest] = relocations.get(mapping[4] + relative + 8, host)
names = ['VdSwap', 'KeWaitForSingleObject', 'KeWaitForMultipleObjects',
         'NtWaitForSingleObjectEx', 'NtWaitForMultipleObjectsEx',
         'KeDelayExecutionThread', 'RtlEnterCriticalSection']
source = args.mapping_source.read_text()
for name in names:
    match = re.search(r'\{\s*(0x[0-9A-Fa-f]+),\s*__imp__' + name + r'\s*\}', source)
    if not match:
        raise SystemExit('Missing guest import mapping: ' + name)
    expected = symbols['__wrap___imp__' + name][4]
    if entries.get(int(match[1], 16)) != expected:
        raise SystemExit('Guest mapping does not reference wrapper: ' + name)
    print('Verified wrapped guest import:', name)
