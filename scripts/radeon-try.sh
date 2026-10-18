#!/bin/bash
# Reboot the test server, bind radeon (optionally a locally built RADEON_KO) with the given module parameters and
# report whether UVD comes up. Needs ssh to arch-server only (no mount).
#   RADEON_KO=/path/radeon.ko scripts/radeon-try.sh "dpm=0 ..."
set -u
params=${1:-}
ko=${RADEON_KO:-}
ssh arch-server "cd osx-gpu && scripts/host-reboot-note.sh write 'radeon-try: $params' >/dev/null && sync && (sleep 1; sudo systemctl reboot) >/dev/null 2>&1 &"
sleep 45
for i in $(seq 1 40); do ssh -o ConnectTimeout=3 -o BatchMode=yes arch-server true 2>/dev/null && break; sleep 5; done
ssh arch-server "cd osx-gpu && scripts/card-quiet.sh apply >/dev/null 2>&1; RADEON_KO='$ko' RADEON_PARAMS='$params' scripts/uvd-ref-linux.sh bind >/dev/null 2>&1"
sleep 5
echo "== radeon $params"
ssh arch-server 'sudo dmesg | grep -aE "UVD init|ring test on 5|UVD.*(fail|error|timeout)|failed init|not responding, giving up" | tail -4; sudo lspci -s 10:00.0 -vv | grep -m1 LnkSta:; uptime -s'
