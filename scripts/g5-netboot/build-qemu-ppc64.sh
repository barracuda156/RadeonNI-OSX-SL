#!/bin/bash
# qemu-system-ppc64 for testing the deployment initramfs (pseries), built
# out of tree into build/qemu-ppc64 from the QEMU source scripts/build-qemu.sh
# unpacked. The shared third_party/qemu build is not touched.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
src=$(cd "$root/third_party/qemu-11.1.1" && pwd -P)
out=$root/build/qemu-ppc64
mkdir -p "$out"
cd "$out"
if [ ! -f build.ninja ]; then
    "$src/configure" --target-list=ppc64-softmmu --enable-slirp --enable-vnc \
        --disable-docs --disable-werror --disable-user
fi
nice ninja -j8 qemu-system-ppc64
ls -la qemu-system-ppc64
