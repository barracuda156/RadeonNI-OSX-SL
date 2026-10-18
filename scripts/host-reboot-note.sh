#!/bin/bash
# Survive a reboot of the server (where the session itself may restart): leave a note with the
# boot id and what to do next, and check it first thing afterwards.
#
#   scripts/host-reboot-note.sh write "what to do after the reboot"   (run BEFORE the reboot)
#   scripts/host-reboot-note.sh check                                   (run FIRST after a restart)
#
# `check` says whether the machine has rebooted since `write` (the kernel's boot id changes),
# when it booted, why the previous boot ended (clean or reset), and prints the note.
# Copyright (c) 2026 kouta-kun and Claude, MIT.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
note=$root/build/reboot-note.txt
boot_id=$(cat /proc/sys/kernel/random/boot_id)
case "${1:-check}" in
write)
    mkdir -p "$root/build"
    { echo "boot_id=$boot_id"; echo "written=$(date '+%F %T')"; echo "uptime_then=$(uptime -p)"; echo "---"; echo "${2:?what to do next}"; } > "$note"
    echo "note written for boot $boot_id"
    ;;
check)
    [ -f "$note" ] || { echo "no reboot note"; exit 0; }
    old=$(sed -n 's/^boot_id=//p' "$note")
    echo "now: $(date '+%F %T'), $(uptime -p), booted $(uptime -s)"
    if [ "$old" = "$boot_id" ]; then
        echo "NOT REBOOTED since the note was written ($(sed -n 's/^written=//p' "$note"))"
    else
        echo "REBOOTED since the note was written ($(sed -n 's/^written=//p' "$note"))"
        echo "previous boot ended: $(journalctl -k -b 0 --no-pager 2>/dev/null | grep -a 'Previous system reset reason' | sed 's/.*reason //' | head -1)"
    fi
    echo "--- note:"; sed '1,/^---$/d' "$note"
    ;;
*) sed -n '2,/^set /p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
