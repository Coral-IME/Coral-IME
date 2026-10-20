"""Read PE imports and reject dependencies on the removed engine DLL."""
from pathlib import Path
import argparse
import json
import struct

def imported_libraries(path):
    data = Path(path).read_bytes()
    def take(offset, count):
        if offset < 0 or count < 0 or offset + count > len(data):
            raise ValueError('Truncated PE: ' + str(path))
        return data[offset:offset + count]
    def u32(offset):
        return struct.unpack('<I', take(offset, 4))[0]
    if take(0, 2) != b'MZ':
        raise ValueError('Missing DOS header: ' + str(path))
    pe = u32(0x3c)
    if take(pe, 4) != b'PE\0\0':
        raise ValueError('Missing PE header: ' + str(path))
    sections = struct.unpack('<H', take(pe + 6, 2))[0]
    optional_size = struct.unpack('<H', take(pe + 20, 2))[0]
    optional = pe + 24
    magic = struct.unpack('<H', take(optional, 2))[0]
    directory = 112 if magic == 0x20b else 96 if magic == 0x10b else None
    if directory is None or sections > 96 or optional_size < directory + 16:
        raise ValueError('Unsupported PE layout: ' + str(path))
    take(optional, optional_size)
    header_size = u32(optional + 60)
    ranges = []
    for index in range(sections):
        record = optional + optional_size + index * 40
        take(record, 40)
        ranges.append((u32(record + 12), u32(record + 16), u32(record + 20)))
    def raw_offset(rva, count):
        if rva < header_size and rva + count <= header_size:
            take(rva, count)
            return rva
        for start, raw_size, raw in ranges:
            if start <= rva and rva - start + count <= raw_size:
                offset = raw + rva - start
                take(offset, count)
                return offset
        raise ValueError('Unmapped PE RVA: ' + str(path))
    rva, size = struct.unpack('<II', take(optional + directory + 8, 8))
    if rva == 0 and size == 0:
        return []
    if rva == 0 or size < 20 or size > 1024 * 1024:
        raise ValueError('Invalid PE imports: ' + str(path))
    libraries = []
    for index in range(min(size // 20, 4096)):
        descriptor = struct.unpack('<IIIII', take(raw_offset(rva + index * 20, 20), 20))
        if not any(descriptor):
            return sorted(set(libraries))
        name_rva = descriptor[3]
        name = bytearray()
        for offset in range(512):
            byte = data[raw_offset(name_rva + offset, 1)]
            if byte == 0:
                if not name:
                    raise ValueError('Empty PE import name')
                libraries.append(name.decode('ascii').lower())
                break
            name.append(byte)
        else:
            raise ValueError('Unterminated PE import name')
    raise ValueError('Unterminated PE import directory')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binaries', nargs='+', type=Path)
    args = parser.parse_args()
    results = {str(path): imported_libraries(path) for path in args.binaries}
    removed = {'langengine.dll', 'coral_engine_core.dll'}
    for path, imports in results.items():
        if removed.intersection(imports):
            parser.error('Removed engine DLL dependency: ' + path)
    print(json.dumps(results, indent=2))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
