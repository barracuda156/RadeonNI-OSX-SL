#!/bin/bash
# Collect, from a Mac OS X guest, what Apple's own Radeon R9700 stack
# publishes and exports, to compare 10.4 with 10.5 (docs/LEOPARD.md).
#
#   scripts/gl-study.sh tiger|leopard        -> build/glstudy/<which>/
#
# The stack compared is ATIRadeon9700 (the same family exists on both).
# Read-only on the guest: plist, nm, otool, strings.

set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
which=${1:?tiger or leopard}
case "$which" in
tiger) ssh() { "$root/scripts/tiger.sh" ssh "$@"; } ;;
leopard) ssh() { "$root/scripts/leopard-guest.sh" ssh "$@"; } ;;
*) echo "tiger or leopard" >&2; exit 1 ;;
esac
out=$root/build/glstudy/$which
mkdir -p "$out"
E=/System/Library/Extensions
GL=/System/Library/Frameworks/OpenGL.framework/Resources

ssh "cat $E/ATIRadeon9700.kext/Contents/Info.plist" > "$out/R9700.kext.Info.plist" 2>/dev/null || true
ssh "cat $E/ATIRadeon9700GLDriver.bundle/Contents/Info.plist" > "$out/R9700GL.Info.plist" 2>/dev/null || true
ssh "cat $E/ATIRadeon9700GA.plugin/Contents/Info.plist" > "$out/R9700GA.Info.plist" 2>/dev/null || true
ssh "ls -la $E/ATIRadeon9700.kext/Contents/MacOS $E/ATIRadeon9700GLDriver.bundle/Contents/MacOS $E/ATIRadeon9700GA.plugin/Contents/MacOS" > "$out/files.txt" 2>/dev/null || true
for pair in "GLDriver:$E/ATIRadeon9700GLDriver.bundle/Contents/MacOS/ATIRadeon9700GLDriver" \
            "GLEngine:$GL/GLEngine.bundle/GLEngine" \
            "GLRendererFloat:$GL/GLRendererFloat.bundle/GLRendererFloat" \
            "kext:$E/ATIRadeon9700.kext/Contents/MacOS/ATIRadeon9700"; do
    n=${pair%%:*}; f=${pair#*:}
    ssh "nm -arch ppc $f 2>/dev/null" > "$out/$n.nm" 2>/dev/null || ssh "nm $f" > "$out/$n.nm" 2>/dev/null || true
    ssh "otool -arch ppc -L $f 2>/dev/null || otool -L $f" > "$out/$n.libs" 2>/dev/null || true
    ssh "strings -a $f | grep -E '^(IO|GLD|gld|kIO|AGP|Accel|NVAccel|ATI)[A-Za-z0-9_:]{3,}$|^IO[A-Z]' | sort -u" > "$out/$n.strings" 2>/dev/null || true
done
ssh "sw_vers -productVersion; uname -r" > "$out/version.txt" 2>/dev/null || true
wc -l "$out"/* | tail -20
