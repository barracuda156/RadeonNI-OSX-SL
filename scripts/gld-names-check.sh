#!/bin/bash
# dlopen a bundle on a guest and check that every gld* name Leopard's
# GLEngine looks up is there (build/glstudy/leopard/GLEngine.gldnames, from
# scripts/gl-study.sh). The engine drops a plug-in that lacks one.
#
#   scripts/gld-names-check.sh tiger|leopard <dylib>
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
which=${1:?tiger or leopard}
lib=${2:?bundle binary}
case "$which" in
tiger) g() { "$root/scripts/tiger.sh" ssh "$@"; } ;;
leopard) g() { "$root/scripts/leopard-guest.sh" ssh "$@"; } ;;
esac
names=$root/build/glstudy/leopard/GLEngine.gldnames
g 'cat > /tmp/gldcheck.c' <<'C'
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
	char line[128];
	FILE *f = fopen("/tmp/gldnames.txt", "r");
	void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	int have = 0, miss = 0;

	if (!h) {
		printf("dlopen failed: %s\n", dlerror());
		return 1;
	}
	while (f && fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (dlsym(h, line)) {
			have++;
		} else {
			printf("  missing: %s\n", line);
			miss++;
		}
	}
	printf("dlopen ok; present %d missing %d\n", have, miss);
	return miss != 0;
}
C
g 'cat > /tmp/gldnames.txt' < "$names"
g 'cat > /tmp/gldcheck.dylib' < "$lib"
g 'cd /tmp && gcc -arch ppc -o gldcheck gldcheck.c && ./gldcheck /tmp/gldcheck.dylib 2>&1 | grep -v "^gldcheck.*malloc"'
