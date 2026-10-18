#!/bin/bash
# From the PC: reboot the test server, bind radeon with the given module parameters and report
# whether UVD comes up (the card is driverless after a boot, radeon is blacklisted).
#   scripts/radeon-try.sh "dpm=0 pcie_gen2=0 ..."
# Needs the repo mounted at the same path as on the server and ssh to arch-server (see docs/HARNESS.md).
set -u
params=${1:-}
cd "$(dirname "$0")/.." || exit 1
ssh arch-server "cd osx-gpu && scripts/host-reboot-note.sh write 'radeon-try: $params' >/dev/null && sync && (sleep 1; sudo systemctl reboot) >/dev/null 2>&1 &"
sleep 45
for i in $(seq 1 40); do ssh -o ConnectTimeout=3 -o BatchMode=yes arch-server true 2>/dev/null && break; sleep 5; done
fusermount3 -uz "$PWD" 2>/dev/null
sshfs -o reconnect,ServerAliveInterval=5,ServerAliveCountMax=3 arch-server:/home/kouta/osx-gpu "$PWD"
cd "$PWD" || exit 1
name=rt$(date +%H%M%S)
scripts/run-remote.sh "$name" env "RADEON_PARAMS=$params" scripts/uvd-ref-linux.sh bind > /dev/null 2>&1
sleep 5
echo "== radeon $params"
ssh arch-server 'sudo dmesg | grep -aE "UVD init|ring test on 5|UVD.*(fail|error|timeout)|\*ERROR\* .*uvd" | tail -4; sudo lspci -s 10:00.0 -vv | grep -m1 LnkSta:; uptime -s'
