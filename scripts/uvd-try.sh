#!/bin/bash
# One run of `rdn_tool uvd` on the host with an optional register replay (docs/JOURNAL.md,
# 2026-10-18). The card must be posted already (rdn_tool post after a fresh boot).
#   scripts/uvd-try.sh [RANGES [FLAGS]]     RANGES like 8000-a3ff,2700-2fff (hex), empty for none
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
sync
if [ -n "${1:-}" ]; then
    export RDN_REPLAY=${RDN_REPLAY_FILE:-$root/build/uvd-ref/replay-missing.txt} RDN_REPLAY_RANGES=$1
fi
timeout 200 sudo -E build/x86/rdn_tool -s 0000:10:00.0 -b private/vbios.rom uvd ${2:-0} 2>&1 | grep -E "accel init|replayed|uvd init|uvd IB|UVD_STATUS|rdn error: uvd"
