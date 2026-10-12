/*
 * holdgl: render something simple on a renderer and keep the context alive,
 * so that another program (rdnuc find, peek) can look at video memory.
 *
 *   holdgl [renderer-id [seconds]]
 *
 * An off-screen 64x64 context: clear to blue (0,0,1), a red triangle over the
 * lower left half, glFinish, then the four corner pixels as glReadPixels
 * gives them; then it waits. Build like prelist.c.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

#define W 64
#define H 64

int main(int argc, char **argv)
{
	long renderer = argc > 1 ? strtol(argv[1], NULL, 0) : 0x21a00;
	int hold = argc > 2 ? atoi(argv[2]) : 25;
	CGLPixelFormatAttribute attrs[] = {
		kCGLPFAOffScreen, kCGLPFAColorSize, 32, kCGLPFARendererID,
		(CGLPixelFormatAttribute)renderer, 0
	};
	CGLPixelFormatObj pix = NULL;
	CGLContextObj ctx = NULL;
	GLint npix = 0;
	unsigned *buf = calloc(W * H, 4);
	unsigned char p[4];

	if (CGLChoosePixelFormat(attrs, &pix, &npix) || !pix ||
	    CGLCreateContext(pix, NULL, &ctx) || !ctx) {
		fprintf(stderr, "no context on renderer 0x%lx\n", renderer);
		return 1;
	}
	CGLSetCurrentContext(ctx);
	CGLSetOffScreen(ctx, W, H, W * 4, buf);
	glViewport(0, 0, W, H);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColor3f(1.0f, 0.0f, 0.0f);
	glBegin(GL_TRIANGLES);
	glVertex2f(-1.0f, -1.0f);
	glVertex2f(1.0f, -1.0f);
	glVertex2f(-1.0f, 1.0f);
	glEnd();
	glFinish();
	printf("GL_RENDERER: %s, error 0x%x\n", (const char *)glGetString(GL_RENDERER), glGetError());
	glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	printf("pixel (8,8) by glReadPixels: %02x%02x%02x%02x\n", p[0], p[1], p[2], p[3]);
	glReadPixels(56, 56, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	printf("pixel (56,56) by glReadPixels: %02x%02x%02x%02x\n", p[0], p[1], p[2], p[3]);
	printf("program buffer words: %08x %08x %08x\n", buf[8 * W + 8], buf[56 * W + 56], buf[0]);
	printf("HOLDING %d s\n", hold);
	fflush(stdout);
	sleep(hold);
	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	return 0;
}
