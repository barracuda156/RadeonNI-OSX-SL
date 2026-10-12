#!/bin/bash
# On the Leopard guest: where does GLEngine keep its table of gld* names, and
# what surrounds it? Prints section sizes and the words around the name
# "gldCreateProgram" in each data section, ppc slice. Read-only.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
E=/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine
"$root/scripts/leopard-guest.sh" ssh "otool -arch ppc -l $E | grep -E 'sectname|segname|addr|size' | paste - - - - | head -40; echo ---; strings -a -t x -arch ppc $E 2>/dev/null | grep -m3 gldCreateProgram; otool -arch ppc -s __TEXT __cstring -v $E | grep -n -m3 gldCreateProgram"
