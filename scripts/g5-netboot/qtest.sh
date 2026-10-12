#!/bin/bash
# Boot the deployment kernel + initramfs in QEMU's pseries machine, with
# virtio devices and a serial console, against a sparse "250 GB disk" that
# starts with the G5's real partition map. The guest connects back to the
# host's loopback (10.0.2.2) on port 9100. Test only; nothing real is touched.
#
#   scripts/g5-netboot/qtest.sh        (serial console on stdout)

set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
N=$root/build/netboot
disk=$N/fake-g5.img
if [ ! -f "$disk" ]; then
    truncate -s 250059350016 "$disk"
    dd if="$root/build/g5deploy/g5-map-backup.bin" of="$disk" bs=512 count=64 conv=notrunc status=none
fi
exec "$root/build/qemu-ppc64/qemu-system-ppc64" \
    -L "$(cd "$root/third_party/qemu-11.1.1/pc-bios" && pwd -P)" \
    -M pseries -cpu POWER9 -m 2048 -nographic -no-reboot \
    -kernel "$N/vmlinuz" -initrd "$N/initrd.img" \
    -append "rdinit=/init console=hvc0 panic=0 rdn.wait=900" \
    -drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2260-:22 -device virtio-net-pci,netdev=n0
