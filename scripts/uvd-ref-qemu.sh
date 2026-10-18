#!/bin/bash
# Run the HD 7570 under a QEMU/KVM guest with the server's own kernel, to see Linux's UVD
# bring-up with the card isolated from the host's drivers (docs/UVD.md).
#
#   scripts/uvd-ref-qemu.sh build            initramfs (no card needed)
#   scripts/uvd-ref-qemu.sh novfio           boot the guest WITHOUT the card: checks the image
#   scripts/uvd-ref-qemu.sh run [FILE]       with the card on vfio-pci (scripts/card-bind.sh vfio
#                                            first; refuses otherwise). FILE (a path on the host)
#                                            is decoded through VA-API and compared with software
#   scripts/uvd-ref-qemu.sh trace [FILE]     the same with every register access traced to
#                                            traces/uvd-ref-qemu.log (slow; needs a QEMU with
#                                            vfio trace events)
#
# The guest mounts the host's root read-only over 9p and has one writable share,
# build/uvd-guest/out. Host rules (CLAUDE.md): `run` and `trace` drive the card's command
# processor: scripts/card-quiet.sh apply first, and the user's yes.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
work=$root/build/uvd-guest
kver=$(uname -r)
VGA=${CARD_VGA:-0000:10:00.0}
AUDIO=${CARD_AUDIO:-0000:10:00.1}

build() {
    local t=$work/initramfs.d
    rm -rf "$t"; mkdir -p "$t"/{bin,dev,proc,sys,host,lib/modules/$kver}
    # mkinitcpio's busybox is dynamic and has no mount: take its sh, and the host's own mount,
    # switch_root and kmod (insmod), with every library they load
    cp /usr/lib/initcpio/busybox "$t/bin/busybox"
    (cd "$t/bin" && ln -sf busybox sh && ln -sf busybox ash)
    local b l
    for b in /usr/bin/mount /usr/bin/switch_root /usr/bin/kmod /usr/bin/sleep /usr/lib/initcpio/busybox; do
        case $b in */busybox) ;; *) cp "$b" "$t/bin/$(basename $b)" ;; esac
        for l in $(ldd "$b" | grep -o '/[^ ]*'); do
            mkdir -p "$t$(dirname "$l")"; cp -L "$l" "$t$l"
        done
    done
    (cd "$t/bin" && ln -sf kmod insmod)
    # modules: virtio, 9p and what they need, as plain .ko (busybox cannot read .zst)
    local mods="virtio virtio_ring virtio_pci virtio_pci_legacy_dev virtio_pci_modern_dev netfs fscache 9pnet 9pnet_virtio 9p"
    local m f
    for m in $mods; do
        f=$(find /usr/lib/modules/$kver -name "$m.ko*" | head -1)
        [ -n "$f" ] || { echo "module $m not found (built in?)" >&2; continue; }
        case "$f" in *.zst) zstd -dq "$f" -o "$t/lib/modules/$kver/$m.ko" ;; *) cp "$f" "$t/lib/modules/$kver/$m.ko" ;; esac
    done
    cat > "$t/init" <<'INIT'
#!/bin/sh
export PATH=/bin
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev
for m in virtio virtio_ring virtio_pci_modern_dev virtio_pci_legacy_dev virtio_pci netfs fscache 9pnet 9pnet_virtio 9p; do
    [ -f /lib/modules/*/$m.ko ] && insmod /lib/modules/*/$m.ko 2>/dev/null
done
mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144,ro host /host || { echo "cannot mount the host root"; sleep 600; }
exec switch_root /host /home/kouta/osx-gpu/scripts/uvd-guest-run.sh
INIT
    chmod +x "$t/init"
    (cd "$t" && find . -print0 | cpio --null -o -H newc --owner 0:0 2>/dev/null) | zstd -q -3 -f -o "$work/initramfs.img"
    ls -la "$work/initramfs.img"
}

qemu_cmd() {
    mkdir -p "$work/out"
    q=(qemu-system-x86_64 -enable-kvm -cpu host -smp 4 -m 4G -nographic -no-reboot
       -kernel /boot/vmlinuz-linux-lts -initrd "$work/initramfs.img"
       -append "console=ttyS0 rdinit=/init panic=-1 uvd.decode=${1:-none}"
       -fsdev "local,id=hostroot,path=/,security_model=none,readonly=on" -device virtio-9p-pci,fsdev=hostroot,mount_tag=host
       -fsdev "local,id=out,path=$work/out,security_model=none" -device virtio-9p-pci,fsdev=out,mount_tag=out)
}

case "${1:-}" in
build) build ;;
novfio)
    [ -f "$work/initramfs.img" ] || build
    qemu_cmd; "${q[@]}"
    ;;
run|trace)
    drv=$(basename "$(readlink /sys/bus/pci/devices/$VGA/driver 2>/dev/null)" 2>/dev/null || true)
    [ "$drv" = vfio-pci ] || { echo "$VGA is bound to '${drv:-nothing}', not vfio-pci: scripts/card-bind.sh vfio first (needs a driverless card, i.e. a fresh boot)" >&2; exit 1; }
    [ -f "$work/initramfs.img" ] || build
    qemu_cmd "${2:-none}"
    # the topology of scripts/x86-trace-guest.sh: both functions behind a PCI bridge; QEMU runs
    # as root (/dev/vfio/N is root-only), so the share it writes is given back afterwards
    extra=(-device pci-bridge,id=br1,chassis_nr=1,addr=0x10
           -device "vfio-pci,host=$VGA,bus=br1,addr=01.0"
           -device "vfio-pci,host=$AUDIO,bus=br1,addr=02.0")
    if [ "$1" = trace ]; then
        mkdir -p "$root/traces"
        extra=(-device pci-bridge,id=br1,chassis_nr=1,addr=0x10
               -device "vfio-pci,host=$VGA,bus=br1,addr=01.0,x-no-mmap=on"
               -device "vfio-pci,host=$AUDIO,bus=br1,addr=02.0"
               -trace vfio_region_read -trace vfio_region_write
               -trace vfio_pci_read_config -trace vfio_pci_write_config
               -msg timestamp=on -D "$root/traces/uvd-ref-qemu.log")
    fi
    sudo timeout 600 "${q[@]}" "${extra[@]}" || echo "qemu exit: $?"
    sudo chown -R "$(id -u):$(id -g)" "$work/out" "$root/traces" 2>/dev/null || true
    ;;
*)
    sed -n '2,/^set /p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
