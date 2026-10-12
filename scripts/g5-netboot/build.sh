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
for p in linux-ppc64 mkinitcpio-busybox glibc zlib libxcrypt gnu-netcat powerpc-utils grub \
         util-linux util-linux-libs systemd-libs kmod zstd xz openssl brotli \
         openssh pam krb5 audit e2fsprogs keyutils libcap-ng; do
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
# mount and insmod are not in mkinitcpio's busybox: util-linux's mount and
# kmod, with the libraries they pull in
for l in util-linux-libs/usr/lib/libmount.so.1 util-linux-libs/usr/lib/libblkid.so.1 \
         systemd-libs/usr/lib/libsystemd.so.0 kmod/usr/lib/libkmod.so.2 \
         zstd/usr/lib/libzstd.so.1 xz/usr/lib/liblzma.so.5 openssl/usr/lib/libcrypto.so.3 \
         brotli/usr/lib/libbrotlienc.so.1 brotli/usr/lib/libbrotlidec.so.1 brotli/usr/lib/libbrotlicommon.so.1; do
    cp -L "x/$l" "$R/lib/"
done
cp x/util-linux/usr/bin/mount "$R/bin/mount"
cp x/kmod/usr/bin/kmod "$R/bin/kmod"
for a in insmod modprobe lsmod rmmod depmod; do ln -s kmod "$R/bin/$a"; done
cp x/mkinitcpio-busybox/usr/lib/initcpio/busybox "$R/bin/busybox"
cp "$(find x/gnu-netcat -name netcat -type f | head -1)" "$R/bin/netcat"
cp x/powerpc-utils/usr/bin/nvram "$R/bin/nvram"

# sshd (key login only) and the libraries it pulls in, whatever package has them
mkdir -p "$R/usr/bin" "$R/lib/ssh" "$R/etc/ssh" "$R/root/.ssh" "$R/var/empty" "$R/dev/pts" "$R/usr/share/empty.sshd"
cp x/openssh/usr/bin/sshd x/openssh/usr/bin/ssh-keygen "$R/usr/bin/"
cp x/openssh/usr/lib/ssh/sshd-session x/openssh/usr/lib/ssh/sshd-auth x/openssh/usr/lib/ssh/sftp-server "$R/lib/ssh/"
python3 -I - "$N" "$R" <<'PY'
import glob, os, re, shutil, subprocess, sys
N, R = sys.argv[1:]
libs = {}
for p in glob.glob(N + '/x/*/usr/lib/*.so*'):
    libs.setdefault(os.path.basename(p), p)
def needed(f):
    out = subprocess.run(['readelf', '-d', f], capture_output=True, text=True).stdout
    return re.findall(r'NEEDED.*\[(.*?)\]', out)
todo = [R + '/usr/bin/sshd', R + '/usr/bin/ssh-keygen'] + glob.glob(R + '/lib/ssh/*')
done, missing = set(), set()
while todo:
    f = todo.pop()
    for n in needed(f):
        if n in done or n in missing or os.path.exists(R + '/lib/' + n):
            continue
        p = libs.get(n)
        if not p:
            missing.add(n)
            continue
        shutil.copy(os.path.realpath(p), R + '/lib/' + n)
        done.add(n)
        todo.append(R + '/lib/' + n)
print('sshd libraries copied:', len(done), 'missing:', sorted(missing))
PY
cat > "$R/etc/ssh/sshd_config" <<'EOF'
Port 22
HostKey /etc/ssh/ssh_host_ed25519_key
HostKey /etc/ssh/ssh_host_rsa_key
PermitRootLogin prohibit-password
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM no
PubkeyAuthentication yes
AuthorizedKeysFile /root/.ssh/authorized_keys
StrictModes no
PrintMotd no
SetEnv PATH=/bin:/sbin:/usr/bin:/usr/sbin
Subsystem sftp /usr/lib/ssh/sftp-server
EOF
printf 'root:*:0:0:root:/root:/bin/sh\nsshd:*:74:74:sshd:/var/empty:/bin/false\nnobody:*:65534:65534:nobody:/var/empty:/bin/false\n' > "$R/etc/passwd"
printf 'root:x:0:\nsshd:x:74:\nnobody:x:65534:\n' > "$R/etc/group"
printf 'passwd: files\ngroup: files\nhosts: files\n' > "$R/etc/nsswitch.conf"
printf '/bin/sh\n' > "$R/etc/shells"
# who may log in: the rescue key, the project's key and this user's own keys
: > "$R/root/.ssh/authorized_keys"
for k in "$root/private/ssh/g5rescue.pub" "$root/private/ssh/tiger_rsa.pub" "$HOME"/.ssh/id_*.pub; do
    [ -f "$k" ] && cat "$k" >> "$R/root/.ssh/authorized_keys"
done
chmod 700 "$R/root" "$R/root/.ssh"
echo "authorized keys: $(wc -l < "$R/root/.ssh/authorized_keys")"
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
(cd "$R" && find . -print0 | cpio --null -o -H newc -R 0:0 --quiet | gzip -9) > "$N/initrd.img"
ls -la "$N/vmlinuz" "$N/initrd.img"
