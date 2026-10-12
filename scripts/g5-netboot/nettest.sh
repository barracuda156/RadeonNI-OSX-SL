#!/bin/bash
# Throughput of the host -> G5 (rescue system) path over plain netcat, to
# /dev/null on the G5: 1 GiB of the volume's data, raw and through gzip -1,
# and 1 GiB of its unused tail the same two ways. Writes nothing on the G5.
#
#   scripts/g5-netboot/nettest.sh <g5-address> [host-address]

set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
g5=${1:?G5 address}
host=${2:-192.168.1.158}
raw=$root/build/g5deploy/leopard.raw
vol=134250496
key=$root/private/ssh/g5rescue
ssh_g5() { ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -i "$key" "root@$g5" "$@"; }

run() { # label offset-GiB mode
    local label=$1 off=$2 mode=$3 s e
    local src="dd if=$raw iflag=skip_bytes,count_bytes bs=1M skip=$((vol + off * 1073741824)) count=1073741824 2>/dev/null"
    if [ "$mode" = gz ]; then
        ( eval "$src" | gzip -1 | nc -l "$host" 9098 ) &
    else
        ( eval "$src" | nc -l "$host" 9098 ) &
    fi
    sleep 1
    s=$(date +%s.%N)
    if [ "$mode" = gz ]; then
        ssh_g5 "nc $host 9098 | gzip -dc | dd of=/dev/null bs=1M 2>/dev/null"
    else
        ssh_g5 "nc $host 9098 | dd of=/dev/null bs=1M 2>/dev/null"
    fi
    e=$(date +%s.%N)
    wait
    awk -v s="$s" -v e="$e" -v l="$label" 'BEGIN { d = e - s; printf "%-26s %6.1f s  %6.1f MB/s of volume data\n", l, d, 1073.74 / d }'
}

run "data  (1 GiB in), raw"  1 raw
run "data  (1 GiB in), gzip" 1 gz
run "tail  (20 GiB in), raw" 20 raw
run "tail  (20 GiB in), gzip" 20 gz
