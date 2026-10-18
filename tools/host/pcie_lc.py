#!/usr/bin/env python3
"""Read (only) the HD 7570's PCIe link-controller registers through the PCIE_PORT index/data pair
(MM 0x38 / 0x3c, what radeon's RREG32_PCIE_PORT does): 0xa0 LC_CNTL, 0xa1, 0xa2 LC_LINK_WIDTH_CNTL,
0xa4 LC_SPEED_CNTL. Copyright (c) 2026 kouta-kun and Claude, MIT."""
import mmap, struct, sys
ADDR = "0000:10:00.0"
base = f"/sys/bus/pci/devices/{ADDR}"
if (open(f"{base}/vendor").read().strip(), open(f"{base}/device").read().strip()) != ("0x1002", "0x675d"):
    sys.exit("not the card")
with open(f"{base}/resource2", "r+b") as f:
    m = mmap.mmap(f.fileno(), 0x20000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
    def rd(i):
        struct.pack_into("<I", m, 0x38, i)
        return struct.unpack_from("<I", m, 0x3c)[0]
    for i in (0xa0, 0xa1, 0xa2, 0xa3, 0xa4):
        print(f"0x{i:x} 0x{rd(i):08x}")
