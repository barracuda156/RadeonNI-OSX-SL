#!/usr/bin/env python3
"""Craft the G5's new Apple partition map (blocks 0..63).

  craftmap.py <g5-map-backup.bin> <volume-disk.raw> <out.bin> [device-blocks]

Adds an Apple_HFS entry named "Leopard" for the volume found in the
partition map of <volume-disk.raw> (the HFS+ partition of the VM disk), at
block 67108864 (32 GiB, 1 MiB aligned), and an Apple_Free entry for the rest
of the device. Entries that exist are kept; only the entry count in each
changes. Refuses anything but the map that was checked on 2026-10-12.

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import struct
import sys

NEW_START = 67108864


def entries(d):
    out = []
    for i in range(1, 64):
        e = d[512 * i:512 * i + 512]
        if e[:2] != b"PM":
            break
        start, length = struct.unpack(">II", e[8:16])
        out.append((i, start, length, e[48:80].rstrip(b"\0").decode()))
    return out


def main():
    backup, volume, out = sys.argv[1:4]
    dev_blocks = int(sys.argv[4]) if len(sys.argv) > 4 else 488397168
    d = bytearray(open(backup, "rb").read(512 * 64))
    expect = [(1, 1, 63, "Apple_partition_map"), (2, 64, 262144, "Apple_Free"),
              (3, 262208, 66846640, "Apple_HFS"), (4, 67108848, 16, "Apple_Free")]
    if entries(d) != expect:
        sys.exit("the G5's map is not the one that was checked: %r" % (entries(d),))
    v = open(volume, "rb").read(512 * 64)
    hfs = [e for e in entries(v) if e[3] == "Apple_HFS"]
    if len(hfs) != 1:
        sys.exit("expected one Apple_HFS entry in the volume disk's map")
    vol_start, vol_len = hfs[0][1], hfs[0][2]
    tail = NEW_START + vol_len

    def make(start, length, name, typ, status):
        e = bytearray(512)
        e[0:2] = b"PM"
        e[8:12] = struct.pack(">I", start)
        e[12:16] = struct.pack(">I", length)
        e[16:16 + len(name)] = name.encode()
        e[48:48 + len(typ)] = typ.encode()
        e[84:88] = struct.pack(">I", length)      # logical data blocks
        e[88:92] = struct.pack(">I", status)
        return e

    d[512 * 5:512 * 6] = make(NEW_START, vol_len, "Leopard", "Apple_HFS", 0x40000033)
    d[512 * 6:512 * 7] = make(tail, dev_blocks - tail, "", "Apple_Free", 0)
    for i in range(1, 7):
        d[512 * i + 4:512 * i + 8] = struct.pack(">I", 6)
    open(out, "wb").write(d)
    print("volume: start %d length %d blocks (%.2f GiB) -> G5 block %d"
          % (vol_start, vol_len, vol_len * 512 / 2**30, NEW_START))
    print("volume byte offset in the VM disk: %d, length %d" % (vol_start * 512, vol_len * 512))


main()
