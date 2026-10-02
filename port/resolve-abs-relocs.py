#!/usr/bin/env python3
"""Resolves relocations against absolute symbols in a 32-bit ARM ELF object.

The game's data refers to constants defined in other assembly files (special
ids, item ids, message ids). After a partial link those are relocations of
8, 16 or 32 bits against absolute symbols. The GNU linker applies them, but
the Android linker (lld) rejects the 8 and 16-bit kinds. Their values are
already known, so this writes them into the data and turns each relocation
into R_ARM_NONE.

Usage: resolve-abs-relocs.py IN.o OUT.o
"""
import struct
import sys

R_ARM_NONE, R_ARM_ABS32, R_ARM_ABS16, R_ARM_ABS8 = 0, 2, 5, 8
SHN_ABS = 0xFFF1
SHT_REL = 9
SIZES = {R_ARM_ABS32: (4, '<I'), R_ARM_ABS16: (2, '<H'), R_ARM_ABS8: (1, '<B')}


def main(src, dst):
    data = bytearray(open(src, 'rb').read())
    assert data[:4] == b'\x7fELF' and data[4] == 1 and data[5] == 1, 'need 32-bit little-endian ELF'
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2E)
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize) for i in range(shnum)]

    resolved = {}
    unresolved = {}
    for name, stype, flags, addr, offset, size, link, info, align, entsize in sections:
        if stype != SHT_REL:
            continue
        symtab = sections[link]
        target = sections[info]
        for r in range(offset, offset + size, 8):
            r_offset, r_info = struct.unpack_from('<II', data, r)
            rtype, symidx = r_info & 0xFF, r_info >> 8
            if rtype not in SIZES:
                continue
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
                '<IIIBBH', data, symtab[4] + symidx * 16)
            width, fmt = SIZES[rtype]
            # Symbol 0 is the null symbol: the value is the addend alone.
            if st_shndx != SHN_ABS and symidx != 0:
                # An address. Only the 32-bit kind can hold one, and lld handles it.
                if rtype != R_ARM_ABS32:
                    unresolved[rtype] = unresolved.get(rtype, 0) + 1
                continue
            where = target[4] + r_offset
            addend, = struct.unpack_from(fmt, data, where)
            value = (st_value + addend) & ((1 << (8 * width)) - 1)
            struct.pack_into(fmt, data, where, value)
            struct.pack_into('<II', data, r, r_offset, R_ARM_NONE)
            resolved[rtype] = resolved.get(rtype, 0) + 1

    open(dst, 'wb').write(data)
    print('resolved against absolute symbols:', {k: v for k, v in sorted(resolved.items())})
    if unresolved:
        print('left alone, narrow relocation against a non-absolute symbol:', unresolved)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
