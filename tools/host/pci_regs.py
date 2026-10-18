#!/usr/bin/env python3
"""Read the HD 7570's registers from the host, read only.

    sudo tools/host/pci_regs.py [-d 0000:10:00.0] LIST.txt     offsets from a file
    sudo tools/host/pci_regs.py [-d ...] --range 0xef00 0xf700  every register in a range
    sudo tools/host/pci_regs.py [-d ...] --uvd                  the UVD block and the PLL

Maps BAR2 (the 128 KB register aperture) of the card from sysfs read-only and prints
"0xOFFSET 0xVALUE" lines, the format of tools/guest/rdnreg.c on the G5, so that the two
can be compared (scripts/uvd-ref-linux.sh compare). Reads only; the registers it
touches have no known side effects (no index/data pairs, no FIFOs). Copyright (c)
2026 kouta-kun and Claude, MIT.
"""
import mmap
import struct
import sys

ADDR = "0000:10:00.0"
EXPECT = ("0x1002", "0x675d")


def main(argv):
    addr = ADDR
    if argv[:1] == ["-d"]:
        addr, argv = argv[1], argv[2:]
    if not argv:
        sys.exit(__doc__)
    base = f"/sys/bus/pci/devices/{addr}"
    ident = (open(f"{base}/vendor").read().strip(), open(f"{base}/device").read().strip())
    if ident != EXPECT:
        sys.exit(f"{addr} is {ident}, expected {EXPECT}; refusing")
    offsets = []
    if argv[:1] == ["--range"]:
        lo, hi = int(argv[1], 0), int(argv[2], 0)
        offsets = list(range(lo, hi, 4))
    elif argv[:1] == ["--uvd"]:
        # only registers that radeon itself touches (build/uvd-ref/uvd-known.txt, from the host
        # trace): reading the whole range reset the host twice (docs/JOURNAL.md, 2026-10-18)
        offsets = [int(l, 0) for l in open("build/uvd-ref/uvd-known.txt") if l.strip()]
        offsets += [0x718, 0x71c, 0x720, 0x79c, 0x854]
    elif argv:
        offsets = [int(l.split()[0], 0) for l in open(argv[0]) if l.strip()]
    else:
        sys.exit(__doc__)
    with open(f"{base}/resource2", "rb") as f:
        m = mmap.mmap(f.fileno(), 0x20000, mmap.MAP_SHARED, mmap.PROT_READ)
        for off in offsets:
            if off + 4 > 0x20000 or off & 3:
                print(f"0x{off:x} SKIP")
                continue
            print(f"0x{off:x} 0x{struct.unpack_from('<I', m, off)[0]:08x}")


main(sys.argv[1:])
