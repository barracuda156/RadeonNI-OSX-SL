#!/bin/bash
# The Open Firmware boot loader for the deployment kernel: a GRUB
# (powerpc-ieee1275) image with its configuration inside, built on the
# host from ArchPOWER's grub modules. It finds /rdn/vmlinuz on whichever HFS+
# partition holds it and starts it with /rdn/initrd.img.
#
#   scripts/g5-netboot/grub.sh [seconds]    -> build/netboot/core.elf
#
# seconds: how long the rescue system waits for someone to log in before it
# puts the boot device back and restarts (default 1800).

set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
N=$root/build/netboot
wait=${1:-1800}
mods=$N/x/grub/usr/lib/grub/powerpc-ieee1275
[ -d "$mods" ] || { echo "run scripts/g5-netboot/build.sh first" >&2; exit 1; }

cat > "$N/early.cfg" <<EOF
set timeout=0
insmod part_apple
insmod hfsplus
search --no-floppy --file --set=root /rdn/vmlinuz
echo "rdn: deployment kernel on \$root"
linux /rdn/vmlinuz rdinit=/init console=tty0 panic=60 rdn.wait=$wait
initrd /rdn/initrd.img
boot
EOF
grub-mkimage -O powerpc-ieee1275 -d "$mods" -o "$N/core.elf" -p /rdn -c "$N/early.cfg" \
    part_apple hfsplus search search_fs_file linux normal boot echo sleep ls cat \
    configfile terminal gzio minicmd
ls -la "$N/core.elf"
file "$N/core.elf"
