"""Package a linked ABI-1 app; all offsets include the 64 byte header."""
import pathlib
import struct
import sys
stem, title = sys.argv[1:]
syms = {parts[2]: int(parts[0], 16) for line in pathlib.Path(stem+'.sym').read_text().splitlines() if len(parts := line.split()) == 3}
base = 0xac410000
raw = pathlib.Path(stem+'.raw').read_bytes()
size = syms['_app_image_end'] - base
payload = raw.ljust(size-64, b'\0')
header = [0x524e4131, 1, 192, size, syms['_app_bss_end']-base, base, 0]
header += [syms['app_'+name]-base for name in ('init','input','update','draw','shutdown')]
data = bytearray(struct.pack('>12I16s', *header, title.encode('ascii')[:15])+payload)
h = 2166136261
for b in data:
    h = ((h ^ b)*16777619) & 0xffffffff
struct.pack_into('>I', data, 24, h)
path = pathlib.Path(stem+'.RNA')
path.write_bytes(data)
print(path, len(data), 'bytes')
