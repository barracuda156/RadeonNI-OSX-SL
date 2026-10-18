#!/bin/bash
# Server side of the card test runner. Starts CMD detached from the caller (an ssh drop does not
# kill it) and writes every output line to build/runs/NAME.log with an fdatasync per line, so what a
# reset would lose is at most the line being written. Meant to be started by scripts/run-remote.sh.
#   scripts/run-detached.sh NAME CMD [ARGS...]
#   scripts/run-detached.sh --status NAME      prints "boot=<id> state=<running|exit N|lost>"
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
runs=$root/build/runs
mkdir -p "$runs"
boot=$(cat /proc/sys/kernel/random/boot_id)

if [ "${1:-}" = "--status" ]; then
    n=${2:?name}
    if [ -f "$runs/$n.exit" ]; then
        echo "boot=$boot state=exit $(cat "$runs/$n.exit")"
    elif [ -f "$runs/$n.pid" ] && [ "$(cat "$runs/$n.boot" 2>/dev/null)" = "$boot" ] && kill -0 "$(cat "$runs/$n.pid")" 2>/dev/null; then
        echo "boot=$boot state=running"
    else
        echo "boot=$boot state=lost"
    fi
    exit 0
fi

name=${1:?name}; shift
[ $# -gt 0 ] || { echo "no command" >&2; exit 2; }
log=$runs/$name.log
rm -f "$runs/$name".{exit,pid,boot}
: > "$log"
echo "$boot" > "$runs/$name.boot"
(
    {
        echo "== $name started $(date +%T) boot=$boot: $*"
        "$@"
        echo "== exit $?"
    } 2>&1 | while IFS= read -r line; do
        printf '%s\n' "$line" >> "$log"
        sync -d "$log"
    done
    last=$(tail -n1 "$log")
    case $last in "== exit "*) echo "${last#== exit }" > "$runs/$name.exit" ;; *) echo "?" > "$runs/$name.exit" ;; esac
    sync "$runs/$name.exit"
) < /dev/null > /dev/null 2>&1 &
echo $! > "$runs/$name.pid"
disown
echo "started $name pid $(cat "$runs/$name.pid")"
