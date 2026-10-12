/*
 * glrenderers: the OpenGL renderers the system lists, as CGL sees them.
 *
 *   gcc -isysroot /Developer/SDKs/MacOSX10.5.sdk -arch ppc -o glrenderers \
 *       glrenderers.c -framework OpenGL
 *
 * For every renderer CGLQueryRendererInfo returns: renderer ID, whether it
 * is accelerated, video memory, display mask. With our accelerator running
 * a renderer of ours (0x21a00) has to be in this list. Needs a window server
 * session (launchctl bsexec <loginwindow pid> ./glrenderers).
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */
#include <OpenGL/OpenGL.h>
#include <stdio.h>

int main(void)
{
	CGLRendererInfoObj info;
	GLint n = 0, i, v;
	CGLError e = CGLQueryRendererInfo(0xffffffff, &info, &n);

	if (e) {
		printf("CGLQueryRendererInfo: error %d\n", (int)e);
		return 1;
	}
	printf("%d renderers\n", (int)n);
	for (i = 0; i < n; i++) {
		printf("renderer %d:", (int)i);
		if (!CGLDescribeRenderer(info, i, kCGLRPRendererID, &v))
			printf(" id %#x", (unsigned)v);
		if (!CGLDescribeRenderer(info, i, kCGLRPAccelerated, &v))
			printf(" accelerated %d", (int)v);
		if (!CGLDescribeRenderer(info, i, kCGLRPVideoMemory, &v))
			printf(" vram %d MB", (int)(v >> 20));
		if (!CGLDescribeRenderer(info, i, kCGLRPDisplayMask, &v))
			printf(" mask %#x", (unsigned)v);
		if (!CGLDescribeRenderer(info, i, kCGLRPWindow, &v))
			printf(" window %d", (int)v);
		printf("\n");
	}
	CGLDestroyRendererInfo(info);
	return 0;
}
