#!/bin/bash
# Build the G5 deployment kernel + initramfs from ArchPOWER (powerpc64)
# packages, into build/netboot/:
#
#   scripts/g5-netboot/build.sh
#
# Result: build/netboot/vmlinuz (ArchPOWER's linux-ppc64) and initrd.img
# (busybox, glibc, GNU netcat, powerpc-utils' nvram, the network and disk
# modules of a G5 and of QEMU's pseries, and scripts/g5-netboot/init).
# Packages are downloaded over https from repo.archlinuxpower.org into
# build/netboot/pkgs and unpacked in build/netboot/x.

set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
N=$root/build/netboot
REPO=https://repo.archlinuxpower.org/base/powerpc64
VER=7.1.4-1-ppc64
mkdir -p "$N/pkgs" "$N/x"
cd "$N"

if [ ! -s pkglist.txt ]; then
    curl -sf --max-time 120 "$REPO/" | grep -o 'href="[^"?]*pkg.tar.zst"' |
        sed 's/href="//;s/"$//' > pkglist.txt
fi

# name pattern (exact package name) -> download and unpack
fetch() {
    local f
    f=$(grep -E "^$1-[0-9]" pkglist.txt | grep -v -E 'headers|docs|debug|static' | head -1)
    [ -n "$f" ] || { echo "no package $1" >&2; exit 1; }
    [ -s "pkgs/$f" ] || curl -sfL --max-time 900 -o "pkgs/$f" "$REPO/$f"
    mkdir -p "x/$1"
    [ -e "x/$1/.PKGINFO" ] || bsdtar -xf "pkgs/$f" -C "x/$1" 2>/dev/null || true
}
for p in linux-ppc64 mkinitcpio-busybox glibc zlib libxcrypt gnu-netcat powerpc-utils grub; do
    fetch "$p"
done
cp "x/linux-ppc64/usr/lib/modules/$VER/vmlinuz" vmlinuz

# 1. every module decompressed, for depmod's dependency closure
full=$N/modfull
rm -rf "$full"
mkdir -p "$full/lib/modules/$VER"
cp -a "x/linux-ppc64/usr/lib/modules/$VER/kernel" "$full/lib/modules/$VER/"
cp "x/linux-ppc64/usr/lib/modules/$VER/modules.builtin" "$full/lib/modules/$VER/"
find "$full" -name '*.ko.zst' -print0 | xargs -0 -n 64 -P 8 zstd -dq --rm
depmod -b "$full" "$VER" 2>/dev/null || true

# 2. the closure of the wanted modules
python3 -I - "$full/lib/modules/$VER" "$N/mods.wanted" <<'PY'
import os, sys
d, out = sys.argv[1], sys.argv[2]
dep = {}
for l in open(os.path.join(d, 'modules.dep')):
    a, _, b = l.strip().partition(':')
    dep[a] = b.split()
roots = ['tg3', 'sungem', 'sungem_phy', 'sata_svw', 'pata_macio', 'ahci', 'libahci',
         'libata', 'sd_mod', 'scsi_mod', 'b44', 'r8169', 'sky2', 'e1000e', 'bnx2',
         'usb-storage', 'ohci-pci', 'ehci-pci', 'sg', 'sr_mod',
         'virtio_net', 'virtio_pci', 'virtio_blk', 'virtio_scsi']
byname = {os.path.basename(p)[:-3].replace('-', '_'): p for p in dep}
want, todo = set(), []
for r in roots:
    p = byname.get(r.replace('-', '_'))
    if p:
        todo.append(p)
    else:
        print('no module', r, file=sys.stderr)
while todo:
    p = todo.pop()
    if p not in want:
        want.add(p)
        todo.extend(dep.get(p, []))
open(out, 'w').write('\n'.join(sorted(want)) + '\n')
print(len(want), 'modules')
PY

# 3. the tree
R=$N/initrd-root
rm -rf "$R"
mkdir -p "$R"/{bin,sbin,etc,proc,sys,dev,tmp,root,mnt,lib,usr/bin,usr/share,var/run,lib/modules/$VER}
ln -s lib "$R/lib64"
# ArchPOWER's glibc searches /usr/lib (merged /usr), not /lib
ln -s ../lib "$R/usr/lib"
for f in x/glibc/usr/lib/*.so*; do
    [ -f "$f" ] && cp -L "$f" "$R/lib/"
done
cp -L x/zlib/usr/lib/libz.so.1 x/libxcrypt/usr/lib/libcrypt.so.2 "$R/lib/"
cp x/mkinitcpio-busybox/usr/lib/initcpio/busybox "$R/bin/busybox"
cp "$(find x/gnu-netcat -name netcat -type f | head -1)" "$R/bin/netcat"
cp x/powerpc-utils/usr/bin/nvram "$R/bin/nvram"
chmod 755 "$R"/bin/*
while read -r p; do
    mkdir -p "$R/lib/modules/$VER/$(dirname "$p")"
    cp "$full/lib/modules/$VER/$p" "$R/lib/modules/$VER/$p"
done < mods.wanted
cp "x/linux-ppc64/usr/lib/modules/$VER/modules.builtin" "$R/lib/modules/$VER/"
depmod -b "$R" "$VER" 2>/dev/null || true
install -m 755 "$root/scripts/g5-netboot/init" "$R/init"

# module load order (dependencies first), for init
python3 -I - "$R/lib/modules/$VER" "$R/modules.load" <<'PY'
import os, sys
d, out = sys.argv[1:]
dep = {}
for l in open(os.path.join(d, 'modules.dep')):
    a, _, b = l.strip().partition(':')
    dep[a] = b.split()
order, seen = [], set()
def visit(m):
    if m in seen:
        return
    seen.add(m)
    for x in dep.get(m, []):
        visit(x)
    order.append(m)
for m in sorted(dep):
    visit(m)
open(out, 'w').write('\n'.join(order) + '\n')
PY
(cd "$R" && find . -print0 | cpio --null -o -H newc --quiet | gzip -9) > "$N/initrd.img"
ls -la "$N/vmlinuz" "$N/initrd.img"
