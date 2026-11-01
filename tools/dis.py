#!/usr/bin/env python3
"""Disassemble Sheep.exe: tools/dis.py <exe> <va_hex> [count]  (x86-32, capstone)."""
import struct, sys
import capstone

def main():
    exe, va, n = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 40
    d = open(exe, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    ns = struct.unpack_from('<H', d, pe + 6)[0]
    so = pe + 24 + struct.unpack_from('<H', d, pe + 20)[0]
    for i in range(ns):
        vs, rva, rs, ro = struct.unpack_from('<IIII', d, so + 40 * i + 8)
        if rva <= va - 0x400000 < rva + rs:
            off = ro + va - 0x400000 - rva
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            for k, ins in enumerate(md.disasm(d[off:off + 16 * n], va)):
                if k >= n: break
                print(f'{ins.address:08x}  {ins.mnemonic} {ins.op_str}')
            return
    sys.exit('address not in a section')

main()
