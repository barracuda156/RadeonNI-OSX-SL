#!/bin/bash
# Run a command on the test server as a detached job and follow its log from here (the PC), so a
# server reset costs only the job, not this session. Prints a clear "SERVER RESET" line and then the
# tail of the netconsole log (~/server-netconsole.log, written by `nc -u -l -p 6666 | tee`).
#   scripts/run-remote.sh NAME CMD [ARGS...]     CMD runs on the server in the repo root
# Environment: SERVER (default arch-server), RUN_TIMEOUT seconds (default 600).
set -u
server=${SERVER:-arch-server}
name=${1:?name}; shift
repo=/home/kouta/osx-gpu
timeout=${RUN_TIMEOUT:-600}
q=$(printf ' %q' "$@")

status() { ssh -o ConnectTimeout=5 -o BatchMode=yes "$server" "cd $repo && scripts/run-detached.sh --status $name" 2>/dev/null; }

start=$(ssh -o ConnectTimeout=10 -o BatchMode=yes "$server" "cd $repo && scripts/run-detached.sh $name$q") || { echo "cannot start on $server" >&2; exit 3; }
echo "$start"
boot=$(status | sed -n 's/^boot=\([^ ]*\).*/\1/p')
shown=0
t0=$(date +%s)
while :; do
    out=$(ssh -o ConnectTimeout=5 -o BatchMode=yes "$server" "cd $repo && tail -c +$((shown + 1)) build/runs/$name.log; echo; scripts/run-detached.sh --status $name" 2>/dev/null)
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "-- ssh to $server failed; waiting --"
    else
        st=$(printf '%s\n' "$out" | tail -n1)
        body=$(printf '%s\n' "$out" | sed '$d')
        if [ -n "$body" ]; then
            printf '%s\n' "$body"
            shown=$((shown + ${#body} + 1))
        fi
        cur=$(printf '%s' "$st" | sed -n 's/^boot=\([^ ]*\).*/\1/p')
        if [ -n "$cur" ] && [ "$cur" != "$boot" ]; then
            echo "== SERVER RESET: boot id changed during '$name' (job lost). Last netconsole lines:"
            tail -n 15 ~/server-netconsole.log 2>/dev/null
            exit 4
        fi
        case $st in
        *state=exit*) echo "== job finished: ${st#*state=}"; code=${st##*exit }; [ "$code" -eq "$code" ] 2>/dev/null && exit "$code"; exit 1 ;;
        *state=lost*) echo "== job lost without a reset (killed?)"; exit 5 ;;
        esac
    fi
    [ $(( $(date +%s) - t0 )) -gt "$timeout" ] && { echo "== timeout after ${timeout}s (job may still run)"; exit 6; }
    sleep 2
done
