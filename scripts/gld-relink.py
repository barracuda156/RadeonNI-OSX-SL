#!/usr/bin/env python3
"""Recompile gld/RadeonNIGLDriver.c and relink the finished Mesa bundle.

  scripts/gld-relink.py            -> build/relink/RadeonNIGLDriver.dylib

For changes in gld/ only: the rest of the bundle (Mesa, r600, the winsys) is
the host build already in third_party/mesa-*/build-darwin, mounted read-only
into the toolchain container (scripts/darwin.sh's image). Only the changed
object is compiled, from this checkout's gld/, and the link adds
gld/rdn_gld.exports so that only the gld* names leave the bundle. The shared
Mesa tree is not written to.

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import glob
import os
import shlex
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
main = os.path.dirname(os.path.realpath(os.path.join(root, 'firmware')))
mesa = sorted(glob.glob(os.path.join(main, 'third_party', 'mesa-*', 'build-darwin')))[-1]
out = os.path.join(root, 'build', 'relink')
os.makedirs(out, exist_ok=True)
target = 'src/gallium/targets/rdn/RadeonNIGLDriver.dylib'


def ninja_commands():
    r = subprocess.run(['ninja', '-t', 'commands', target], cwd=mesa,
                       capture_output=True, text=True, check=True)
    return r.stdout.strip().split('\n')


cmds = ninja_commands()
comp = [c for c in cmds if 'gld_RadeonNIGLDriver.c.o' in c and ' -c ' in c][-1]
link = cmds[-1]
obj = os.path.join(out, 'gld_RadeonNIGLDriver.c.o')

# compile: this checkout's source and headers first, object into out/
parts = shlex.split(comp)
res = []
i = 0
while i < len(parts):
    a = parts[i]
    if a == '-o':
        res += ['-o', obj]
        i += 2
        continue
    if a in ('-MF', '-MQ'):
        i += 2
        continue
    if a == '-MD':
        i += 1
        continue
    if a.endswith('gld/RadeonNIGLDriver.c'):
        res.append(os.path.join(root, 'gld', 'RadeonNIGLDriver.c'))
        i += 1
        continue
    res.append(a)
    i += 1
res.insert(1, '-I' + os.path.join(root, 'gld'))
compile_cmd = ' '.join(shlex.quote(a) for a in res)

lparts = shlex.split(link)
lres = []
i = 0
while i < len(lparts):
    a = lparts[i]
    if a == '-o':
        lres += ['-o', os.path.join(out, 'RadeonNIGLDriver.dylib'),
                 '-Wl,-exported_symbols_list,' + os.path.join(root, 'gld', 'rdn_gld.exports')]
        i += 2
        continue
    if a.endswith('gld_RadeonNIGLDriver.c.o'):
        lres.append(obj)
        i += 1
        continue
    lres.append(a)
    i += 1
link_cmd = ' '.join(shlex.quote(a) for a in lres)

script = os.path.join(out, 'relink.sh')
with open(script, 'w') as f:
    f.write('#!/bin/sh\nset -e\ncd %s\n%s\n%s\n' % (shlex.quote(mesa), compile_cmd, link_cmd))
os.chmod(script, 0o755)
uid = '%d:%d' % (os.getuid(), os.getgid())
r = subprocess.run(['docker', 'run', '--rm', '--network', 'none', '-u', uid, '-e', 'HOME=/tmp',
                    '-v', main + ':' + main + ':ro', '-v', root + ':' + root + ':ro',
                    '-v', out + ':' + out, '-w', mesa, 'osx-gpu-darwin8', '/bin/sh', script])
if r.returncode:
    sys.exit(r.returncode)
print(os.path.join(out, 'RadeonNIGLDriver.dylib'))
