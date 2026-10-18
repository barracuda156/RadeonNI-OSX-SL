/*
 * gldraw_probe: an interposer that reports what a program's glDrawElements
 * drew. For finding out why a program's picture is black.
 *
 *   gcc -dynamiclib -o gldraw_probe.dylib gldraw_probe.c
 *   DYLD_INSERT_LIBRARIES=./gldraw_probe.dylib program ...
 *
 * With GLDRAW_PROBE_ALPHA1 in the environment, after each 6-index draw it
 * also sets the alpha of the whole frame to 1 (colour mask: alpha only).
 *
 * With GLDRAW_PROBE_NO_ARRAYS, glDrawArrays calls (VLC's subpictures) are dropped.
 *
 * With GLDRAW_PROBE_FINISH, glFinish after every draw.
 *
 * With GLDRAW_PROBE_MAGENTA, the frame is cleared to magenta after the video draw.
 *
 * After every 30th glDrawElements it prints glGetError, the viewport and
 * the pixel under the centre of the viewport, to /tmp/gldraw_probe.log.
 * On Leopard, __interpose replaces the calls the program makes through the
 * OpenGL framework's symbols. No OpenGL headers needed (the G5 has none).
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef unsigned int GLenum;
typedef int GLint, GLsizei;
typedef unsigned char GLubyte;

extern void glDrawElements(GLenum, GLsizei, GLenum, const void *);
extern void glDrawArrays(GLenum, GLint, GLsizei);
extern GLenum glGetError(void);
extern void glGetIntegerv(GLenum, GLint *);
extern void glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
extern void glFinish(void);
extern void glColorMask(unsigned char, unsigned char, unsigned char, unsigned char);
extern void glClearColor(float, float, float, float);
extern void glClear(unsigned int);
extern void glGetFloatv(GLenum, float *);
extern void glGetBooleanv(GLenum, unsigned char *);

static int calls;

static void report(const char *what, int count)
{
	GLint vp[4] = { 0, 0, 0, 0 }, rb = 0, cull = 0, depth = 0, scissor = 0,
	      prog = 0, bufs = 0;
	GLubyte px[4] = { 1, 2, 3, 4 };
	GLenum err;
	FILE *f;

	glGetIntegerv(0x0ba2, vp);		/* GL_VIEWPORT */
	glGetIntegerv(0x0c02, &rb);		/* GL_READ_BUFFER */
	glGetIntegerv(0x0b44, &cull);		/* not a pname for Integerv: ignored */
	glGetIntegerv(0x8b8d, &prog);		/* GL_CURRENT_PROGRAM */
	glGetIntegerv(0x8894, &bufs);		/* GL_ARRAY_BUFFER_BINDING */
	(void)depth; (void)scissor;
	glFinish();
	glReadPixels(vp[0] + vp[2] / 2, vp[1] + vp[3] / 2, 1, 1, 0x1908, 0x1401, px);
	err = glGetError();
	f = fopen("/tmp/gldraw_probe.log", "a");
	if (f) {
		fprintf(f, "%s count %d #%d: viewport %d %d %d %d readbuf 0x%x program %d "
			"arraybuf %d centre %02x %02x %02x %02x err 0x%x\n",
			what, count, calls, vp[0], vp[1], vp[2], vp[3], rb, prog, bufs,
			px[0], px[1], px[2], px[3], err);
		fclose(f);
	}
}

static void my_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *ind)
{
	glDrawElements(mode, count, type, ind);
	if (getenv("GLDRAW_PROBE_FINISH"))
		glFinish();
	if (count == 6 && getenv("GLDRAW_PROBE_MAGENTA")) {
		glClearColor(1, 0, 1, 1);
		glClear(0x4000);
	}
	if (count == 6 && getenv("GLDRAW_PROBE_ALPHA1")) {
		float cc[4];
		unsigned char cm[4];

		glGetFloatv(0x0c22, cc);	/* GL_COLOR_CLEAR_VALUE */
		glGetBooleanv(0x0c23, cm);	/* GL_COLOR_WRITEMASK */
		glColorMask(0, 0, 0, 1);
		glClearColor(0, 0, 0, 1);
		glClear(0x4000);
		glColorMask(cm[0], cm[1], cm[2], cm[3]);
		glClearColor(cc[0], cc[1], cc[2], cc[3]);
	}
	if (count == 6 && ++calls % 30 == 1)
		report("glDrawElements", count);
}

static void my_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (getenv("GLDRAW_PROBE_NO_ARRAYS"))
		return;
	glDrawArrays(mode, first, count);
	if (getenv("GLDRAW_PROBE_FINISH"))
		glFinish();
}

#define INTERPOSE(new, old) \
	__attribute__((used)) static struct { void *n, *o; } _i_##old \
	__attribute__((section("__DATA,__interpose"))) = { (void *)new, (void *)old }

INTERPOSE(my_glDrawElements, glDrawElements);
INTERPOSE(my_glDrawArrays, glDrawArrays);
