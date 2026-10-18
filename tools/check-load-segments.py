"""Reject truncated, misaligned, or overlapping load segments before signing."""
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
    raise SystemExit('Expected a little-endian ELF64 image')
offset = struct.unpack_from('<Q', data, 32)[0]
stride, count = struct.unpack_from('<HH', data, 54)
if stride != 56 or count == 0 or offset + stride * count > len(data):
    raise SystemExit('Invalid program-header table')
loads = []
for index in range(count):
    kind, flags, position, address, _, size, memory, alignment = struct.unpack_from(
        '<II6Q', data, offset + index * stride)
    if kind != 1:
        continue
    if size > memory or position + size > len(data):
        raise SystemExit(f'Truncated load segment {index}')
    if alignment > 1 and (alignment & (alignment - 1) or position % alignment != address % alignment):
        raise SystemExit(f'Misaligned load segment {index}')
    if memory:
        loads.append((address, address + memory, index))
loads.sort()
for left, right in zip(loads, loads[1:]):
    if left[1] > right[0]:
        raise SystemExit(f'Overlapping load segments {left[2]} and {right[2]}')
print(f'Checked {len(loads)} load segments: bounds, alignment and separation passed')
