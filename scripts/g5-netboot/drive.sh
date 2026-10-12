#!/bin/bash
# Host side of the deployment initramfs: wait for it to connect back, send
# the commands in a file, keep the session open for a while, log everything.
#
#   scripts/g5-netboot/drive.sh <listen-addr> <port> <commands-file> <log> [seconds]
#
# The shell on the other end is root and unauthenticated: use it on a LAN you
# trust, for the minutes the job takes, and with the listen address of this
# host's own interface.

set -uo pipefail
addr=${1:?listen address}
port=${2:?port}
cmds=${3:?commands file}
log=${4:?log file}
hold=${5:-30}
{
    sleep 2
    cat "$cmds"
    sleep "$hold"
} | nc -l "$addr" "$port" > "$log" 2>&1
echo "[drive.sh: listener exit $?]" >> "$log"
