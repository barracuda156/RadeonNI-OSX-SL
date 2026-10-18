#!/bin/bash
# Resume the Claude session after a server reset. Run at boot by a systemd unit (see
# scripts/resume-after-reboot.service); it only starts Claude with the note that
# scripts/host-reboot-note.sh wrote, it never touches the card itself. Claude's own tools and
# CLAUDE.md host rules still apply (card steps one at a time, ask before risky ones).
#   scripts/resume-after-reboot.sh SESSION_ID
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
sid=${1:?session id}
note=$root/build/reboot-note.txt
[ -f "$note" ] || exit 0
[ -e build/resume-done ] && [ build/resume-done -nt "$note" ] && exit 0
sleep 60    # let the network and the machine settle after a reset
touch build/resume-done
exec claude --resume "$sid" -p "The server just rebooted (this prompt was sent by scripts/resume-after-reboot.sh). Run scripts/host-reboot-note.sh check, read build/reboot-note.txt, and continue from there under the host safety rules in CLAUDE.md." \
    >> build/resume-after-reboot.log 2>&1
