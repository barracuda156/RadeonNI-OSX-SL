#!/bin/bash
# Leopard (10.5.9, Sorbet) guest beside the Tiger one: same harness as
# scripts/tiger.sh with its own disk, ssh port, VNC display and sockets.
#
#   scripts/leopard-guest.sh run | cdrom <image> | snapshot <n> | restore <n>
#   scripts/leopard-guest.sh ssh [cmd...]
#
# Guest ssh 127.0.0.1:2223, VNC 127.0.0.1:5902, monitor/QMP
# build/leopard-{mon,qmp}.sock, serial build/leopard-serial.log.
# Tiger's guest (port 2222) is never touched. Passthrough is not offered:
# the card goes to one guest at a time and only with the user's yes.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
# images/ lives in the main checkout; a worktree reaches it through the
# git-ignored symlinks (firmware -> <main>/firmware).
main=$(dirname "$(readlink -f "$root/firmware")")
[ -d "$main/images" ] || main=$root

export TIGER_DISK=${LEOPARD_DISK:-$main/images/leopard.qcow2}
export TIGER_SSH_PORT=${LEOPARD_SSH_PORT:-2223}
export TIGER_VNC=${LEOPARD_VNC:-127.0.0.1:2}
export TIGER_SOCK_PREFIX=leopard
export TIGER_SERIAL_LOG=leopard-serial.log
export TIGER_USER=${LEOPARD_USER:-tiger}

case "${1:-}" in
ssh)
    shift
    exec "$root/scripts/mac.sh" guest ssh "$@"
    ;;
passthru)
    echo "passthru is deliberately not offered here" >&2
    exit 1
    ;;
esac
exec "$root/scripts/tiger.sh" "$@"
