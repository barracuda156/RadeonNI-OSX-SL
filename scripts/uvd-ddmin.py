#!/usr/bin/env python3
"""Delta-debug the register stream that wakes the UVD VCPU (docs/JOURNAL.md, 2026-10-18).

Input: build/uvd-ref/rom-min-timed.txt, radeon's accesses after our POST ("W|R 0xOFF 0xVAL dt_us" per line).
Each test: secondary bus reset of the card's slot, rdn_tool post, rdn_tool replay of the kept writes (the firmware
written to VRAM before the HDP flush at original line 789), and success means UVD_STATUS has bit 1.
Reads are dropped except the waits the replay honours (0x718, 0x71c, 0x79c, 0xf6bc); their delays are kept.
Runs from the PC over ssh. Log: build/uvd-ref/ddmin.log on the PC (REPO/…)."""
import re, subprocess, sys, time

REPO = "/home/kouta/osx-gpu"
SRC = f"{REPO}/build/uvd-ref/rom-min-timed.txt"
LOG = "/tmp/rt/ddmin.log"
FW_AT = 789          # the firmware goes in before the first line with this original index or later
LAST = 2520
KEEP_READS = {0x718, 0x71c, 0x79c, 0xf6bc}

lines = [l.split() for l in open(SRC)][: LAST + 1]
ops = [(i, k, int(o, 16), int(v, 16), int(dt)) for i, (k, o, v, dt) in enumerate(lines)]

def build(kept):
    out, acc, fw_done = [], 0, False
    for i, k, o, v, dt in ops:
        acc += dt
        take = (k == "W" and i in kept) or (k == "R" and o in KEEP_READS)
        if not take:
            continue
        if not fw_done and i >= FW_AT:
            out.append("F 0x0 0x0 0\n")
            fw_done = True
        out.append("%s 0x%05x 0x%08x %d\n" % (k, o, v, min(acc, 200000)))
        acc = 0
    if not fw_done:
        out.append("F 0x0 0x0 0\n")
    return "".join(out)

def ssh(cmd, timeout=600):
    return subprocess.run(["ssh", "-o", "ConnectTimeout=10", "arch-server", cmd],
                          capture_output=True, text=True, timeout=timeout).stdout

n_tests = 0
since_boot = 0
def reboot():
    """After many bus resets and replays the card stops behaving (the IOMMU log fills with page faults from the
    interrupt handler's write-back address): reboot the server now and then."""
    global since_boot
    subprocess.run(["ssh", "arch-server", "(sleep 1; sudo systemctl reboot) >/dev/null 2>&1 &"], timeout=30)
    time.sleep(45)
    for _ in range(60):
        r = subprocess.run(["ssh", "-o", "ConnectTimeout=3", "-o", "BatchMode=yes", "arch-server",
                            "cd osx-gpu && scripts/card-bind.sh none >/dev/null 2>&1; echo up"],
                           capture_output=True, text=True)
        if "up" in r.stdout:
            break
        time.sleep(5)
    since_boot = 0

def test(kept):
    global n_tests, since_boot
    if since_boot >= 20:
        reboot()
    since_boot += 1
    n_tests += 1
    body = build(set(kept))
    subprocess.run(["ssh", "arch-server", f"cat > osx-gpu/build/uvd-ref/dd-test.txt"], input=body, text=True, timeout=60)
    out = ssh("cd osx-gpu && sudo scripts/card-reset.sh >/dev/null 2>&1; sleep 3; scripts/card-quiet.sh apply >/dev/null 2>&1; "
              "sudo build/x86/rdn_tool -s 0000:10:00.0 -b private/vbios.rom post >/dev/null 2>&1; "
              "sudo setpci -s 10:00.0 COMMAND=0x04:0x04; "
              "sudo env RDN_REPLAY_TIMED=1 build/x86/rdn_tool -s 0000:10:00.0 -b private/vbios.rom replay "
              "build/uvd-ref/dd-test.txt 0 100000 2>&1 | grep -aE 'replayed'")
    m = re.search(r"UVD_STATUS ([0-9a-f]{8})", out)
    ok = bool(m) and (int(m.group(1), 16) & 2) != 0
    with open(LOG, "a") as f:
        f.write(f"test {n_tests}: {len(kept)} writes -> {'OK' if ok else 'fail'} ({out.strip()[:80]})\n")
    return ok

def ddmin(items):
    n = 2
    while len(items) >= 2:
        size = len(items) // n or 1
        subsets = [items[i:i + size] for i in range(0, len(items), size)]
        reduced = False
        for s in subsets:
            if test(s):
                items, n, reduced = s, 2, True
                break
        if not reduced:
            for idx in range(len(subsets)):
                comp = [x for j, s in enumerate(subsets) if j != idx for x in s]
                if comp and test(comp):
                    items, n, reduced = comp, max(n - 1, 2), True
                    break
        if not reduced:
            if n >= len(items):
                break
            n = min(len(items), n * 2)
        with open(LOG, "a") as f:
            f.write(f"-- now {len(items)} writes, n={n}\n")
    return items

if __name__ == "__main__":
    allw = [i for i, k, o, v, dt in ops if k == "W"]
    if len(sys.argv) > 1:      # start from a previous result (RESULT lines of another log)
        txt = open(sys.argv[1]).read()
        allw = [int(m.group(1)) for m in re.finditer(r"line (\d+) W", txt[txt.index("RESULT"):])]
    with open(LOG, "a") as f:
        f.write(f"start: {len(allw)} writes\n")
    if not test(allw):
        with open(LOG, "a") as f:
            f.write("baseline failed; stop\n")
        sys.exit(1)
    res = ddmin(allw)
    with open(LOG, "a") as f:
        f.write("RESULT %d writes:\n" % len(res))
        for i in res:
            _, k, o, v, dt = ops[i]
            f.write("  line %d W 0x%05x 0x%08x\n" % (i, o, v))
        f.write("DONE\n")
