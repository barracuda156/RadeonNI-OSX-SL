#!/bin/bash
# PID 1 of the UVD reference guest (scripts/uvd-ref-qemu.sh): runs from the host's own root
# filesystem, mounted read-only over 9p. Prints its results on the serial console and writes
# files to /out (a read-write 9p share). Nothing here touches the host except through /out.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
export PATH=/usr/local/bin:/usr/bin:/usr/sbin HOME=/tmp
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null
mkdir -p /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
for d in /tmp /run /var/tmp /var/log; do mount -t tmpfs tmpfs $d 2>/dev/null; done
ln -sf /proc/self/fd /dev/fd
# the writable share goes on /mnt (the host's root is read-only, nothing else can be created)
mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144 out /mnt
OUT=/mnt
exec > >(tee $OUT/guest.log) 2>&1

cmd=$(cat /proc/cmdline)
echo "== uvd reference guest: $(uname -r)"
CARD=$(for d in /sys/bus/pci/devices/*; do [ "$(cat $d/vendor)" = 0x1002 ] && [ "$(cat $d/device)" = 0x675d ] && basename $d; done | head -1)
echo "card in the guest: ${CARD:-none}"
if [ -z "$CARD" ]; then
    echo "no card: nothing to test (boot check only)"
else
    modprobe radeon 2>&1 | tail -3
    sleep 3
    dmesg | grep -a -i -E "radeon|uvd|ring test|ib test|\[drm\]" | tail -50
    if [ -n "$(ls /dev/dri/renderD* 2>/dev/null)" ]; then
        python3 -I /home/kouta/osx-gpu/tools/host/pci_regs.py -d "$CARD" --uvd > $OUT/guest-uvd-regs.txt 2>&1
        python3 -I /home/kouta/osx-gpu/tools/host/pci_regs.py -d "$CARD" /home/kouta/osx-gpu/build/uvd-ref/regs_to_read.txt > $OUT/guest-regs.txt 2>&1
        wc -l $OUT/guest-uvd-regs.txt $OUT/guest-regs.txt
        case "$cmd" in
        *uvd.decode=*)
            f=${cmd##*uvd.decode=}; f=${f%% *}
            echo "== ffmpeg VA-API decode of $f"
            ffmpeg -hide_banner -hwaccel vaapi -hwaccel_device "$(ls /dev/dri/renderD* | tail -1)" \
                -hwaccel_output_format vaapi -i "$f" -t 15 -f null - 2>&1 | tail -12
            echo "== same file, software decode"
            ffmpeg -hide_banner -i "$f" -t 15 -f null - 2>&1 | tail -4
            ;;
        esac
    else
        echo "radeon did not create a render node"
    fi
fi
echo "== done"
sync
echo o > /proc/sysrq-trigger
sleep 5
