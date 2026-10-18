#!/bin/bash
# Kernel-side trace of radeon bringing the card up: function_graph over every function of the radeon
# module (plus the PCI config accessors and the cache flush helpers), with durations, from the module
# load to ten seconds after the bind. Shows what the CPU does between the register accesses that
# mmiotrace records (docs/JOURNAL.md, 2026-10-18). Needs the card driverless (fresh boot); the
# parameters of radeon come from RADEON_PARAMS as in scripts/card-bind.sh. Output: traces/uvd-ref-ftrace.txt
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
t=/sys/kernel/tracing
out=${1:-$root/traces/uvd-ref-ftrace.txt}
sudo test -d $t || { echo "no tracefs" >&2; exit 1; }
"$root/scripts/card-quiet.sh" apply > /dev/null
sudo sh -c "echo 0 > $t/tracing_on; echo nop > $t/current_tracer; echo > $t/set_ftrace_filter; echo 65536 > $t/buffer_size_kb"
sudo sh -c "echo ':mod:radeon' > $t/set_ftrace_filter"
for f in 'pci_*config*' wbinvd_on_all_cpus 'clflush_cache_range' 'mem*_toio' 'memset_io' 'msleep' 'usleep_range_state' 'schedule_timeout*'; do
    sudo sh -c "echo '$f' >> $t/set_ftrace_filter" 2>/dev/null || true
done
sudo sh -c "echo function_graph > $t/current_tracer; echo funcgraph-abstime > $t/trace_options 2>/dev/null; echo > $t/trace; echo 1 > $t/tracing_on"
"$root/scripts/card-bind.sh" radeon > /dev/null || true
sleep 10
sudo sh -c "echo 0 > $t/tracing_on; cat $t/trace > '$out'; echo nop > $t/current_tracer; echo > $t/set_ftrace_filter; echo 0 > $t/buffer_size_kb" || true
sudo chown "$(id -u):$(id -g)" "$out"
wc -l "$out"
