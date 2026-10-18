#!/bin/bash
# UVD on the HD 7570 under the server's own Linux (x86): is the block alive, and what state
# does Linux's init leave? Ground truth for the G5 kext, whose VCPU never answers
# (docs/UVD.md, docs/JOURNAL.md 2026-10-17/18).
#
#   scripts/uvd-ref-linux.sh status     what is there (read only; changes nothing)
#   scripts/uvd-ref-linux.sh bind       card-quiet apply, then give the card to radeon
#                                       (HOST CHANGE: needs the user's yes; see below)
#   scripts/uvd-ref-linux.sh log        radeon / UVD lines of the kernel log
#   scripts/uvd-ref-linux.sh dump       registers to build/uvd-ref/linux-regs.txt (read only)
#   scripts/uvd-ref-linux.sh decode F   ffmpeg through VA-API (Mesa's r600 UVD) on file F
#   scripts/uvd-ref-linux.sh compare    Linux's registers against the G5's (build/uvd-ref)
#
# Host rules (CLAUDE.md): the card goes in the CPU-attached x16 slot, the host is NOT rebooted
# by this script, radeon is blacklisted (unbinding it once oopsed the kernel): this only ever
# BINDS it, at runtime, and never unbinds it again; the way back is a host reboot, which needs
# the user's yes. `bind` applies scripts/card-quiet.sh first (the sync-flood rule).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
VGA=${CARD_VGA:-0000:10:00.0}
out=$root/build/uvd-ref
mkdir -p "$out"

need_card() { [ -e /sys/bus/pci/devices/$VGA ] || { echo "$VGA is not there: is the card in the server?" >&2; exit 1; }; }

case "${1:-status}" in
status)
    if [ -e /sys/bus/pci/devices/$VGA ]; then
        echo "card: $(cat /sys/bus/pci/devices/$VGA/vendor):$(cat /sys/bus/pci/devices/$VGA/device)"
        echo "driver: $(basename "$(readlink /sys/bus/pci/devices/$VGA/driver 2>/dev/null)" 2>/dev/null || echo none)"
    else
        echo "card: not present"
    fi
    ls /dev/dri 2>/dev/null | tr '\n' ' '; echo
    ls /lib/firmware/radeon | grep -E "SUMO_uvd|TURKS_(me|pfp|mc|smc)" | tr '\n' ' '; echo
    ls /usr/lib/dri/r600_drv_video.so
    ;;
bind)
    need_card
    "$root/scripts/card-quiet.sh" apply
    "$root/scripts/card-bind.sh" radeon
    ;;
log)
    sudo dmesg | grep -a -i -E "radeon|uvd|\[drm\]" | tail -80
    ;;
dump)
    need_card
    sudo "$root/tools/host/pci_regs.py" -d "$VGA" --uvd > "$out/linux-uvd-regs.txt"
    sudo "$root/tools/host/pci_regs.py" -d "$VGA" --range 0x600 0x700 > "$out/linux-pm-regs.txt"
    sudo "$root/tools/host/pci_regs.py" -d "$VGA" "$out/regs_to_read.txt" > "$out/linux-regs.txt" 2>/dev/null || true
    wc -l "$out"/linux-*.txt
    ;;
decode)
    f=${2:?file required}
    dev=$(ls /dev/dri/renderD* | tail -1)
    echo "render node: $dev"
    ffmpeg -hide_banner -hwaccel vaapi -hwaccel_device "$dev" -hwaccel_output_format vaapi -i "$f" -t 20 -f null - 2>&1 | tail -15
    ;;
compare)
    python3 -I - "$out" <<'PY'
import sys
out = sys.argv[1]
def load(p):
    d = {}
    for l in open(p):
        w = l.split()
        if len(w) == 2 and w[1] not in ("SKIP", "ERR"):
            d[int(w[0], 0)] = int(w[1], 0)
    return d
lin = load(f"{out}/linux-regs.txt")
lin.update(load(f"{out}/linux-uvd-regs.txt"))
g5 = load(f"{out}/ours_regs.txt")
print(f"{len(lin)} Linux registers, {len(g5)} G5 registers, {len(set(lin) & set(g5))} in common")
for off in sorted(set(lin) & set(g5)):
    if lin[off] != g5[off]:
        print(f"0x{off:05x} linux 0x{lin[off]:08x} g5 0x{g5[off]:08x}")
PY
    ;;
*)
    sed -n '2,/^set /p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
