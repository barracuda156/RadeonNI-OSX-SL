#!/bin/bash
# Refuse a kext binary that Mac OS X's own kext linker cannot take.
#
#   scripts/check-kext-relocs.sh <kext binary>
#
# kld (IOKit.framework, kld_patch.c) caches the relocations of the sections
# that can hold vtables, (__TEXT,__const) and (__DATA,__data) among them. For
# every entry that is not a plain 32-bit pointer (scattered, or another type
# or length) relocateSection() leaves a zeroed record, and
# prepareFileForLink() then reads rec->fRInfo->r_address: a NULL
# dereference in kextd and kextcache (SIGBUS, 2026-10-11, Mac OS X 10.5.8).
# A scattered entry appears when static data holds a pointer plus an offset
# to local data, such as a constant array of &table[1]. __text is not
# cached, so its scattered entries (HA16/LO16 pairs) are harmless.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
bin=${1:?usage: $0 <kext binary>}

"$root/scripts/darwin.sh" powerpc-apple-darwin8-otool -rv "$bin" 2>/dev/null |
awk '
    /^Relocation information/ { sec = $0; next }
    sec == "" || sec ~ /__TEXT,__text/ { next }
    $1 ~ /^[0-9a-f]+$/ && NF >= 5 {
        # address pcrel length extern type ...
        if ($4 == "n/a" || $5 != "VANILLA" || $3 != "long") {
            print "  " sec ": " $0
            bad++
        }
    }
    END {
        if (bad) {
            print bad " relocation(s) outside __text that the kext linker crashes on"
            exit 1
        }
        print "kext relocations: no scattered or non-pointer entry outside __text"
    }'
