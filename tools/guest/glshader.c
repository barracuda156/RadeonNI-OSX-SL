/*
 * glshader: does a GLSL fragment shader that samples textures draw?
 *
 *   glshader [width height [hold]]   (texture size, default 64x64; VLC: 704 400;
 *                                     hold: then show a solid window with alpha 0, then 1)
 *
 * What VLC's default OpenGL output does: a #version 120 fragment shader
 * with sampler2D uniforms over GL_RED (or GL_LUMINANCE) textures. Opens a
 * window, draws a quad four ways and prints the centre pixel of each:
 *   0 fixed pipeline, RGBA texture        (expect 33 66 99)
 *   1 shader, constant colour             (expect ff 00 00)
 *   2 shader, texture2D of the RGBA tex   (expect 33 66 99)
 *   3 shader, texture2D of a GL_RED tex   (expect 80 80 80 in .r, shown as grey)
 *   4 shader, three GL_RED textures, YUV to RGB (Y 0x80, U V 0x80: grey 80)
 *   5 as VLC draws: VBOs, generic attributes, matrix uniforms, glDrawElements
 *     (expect 33 66 99)
 * Build in the guest:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o glshader glshader.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <GLUT/glut.h>

#define GL_RED_ 0x1903
#define GL_R8_ 0x8229

static GLuint tex_rgba, tex_y, tex_u, tex_v;
static int red, w = 64, h = 64, hold;

static GLhandleARB compile(GLenum type, const char *src)
{
	GLhandleARB s = glCreateShaderObjectARB(type);
	GLint ok = 0;
	char log[2048] = "";

	glShaderSourceARB(s, 1, &src, NULL);
	glCompileShaderARB(s);
	glGetObjectParameterivARB(s, GL_OBJECT_COMPILE_STATUS_ARB, &ok);
	if (!ok) {
		glGetInfoLogARB(s, sizeof log, NULL, log);
		printf("compile failed: %s\n", log);
	}
	return s;
}

static GLhandleARB program(const char *fs)
{
	static const char *vs =
		"#version 120\n"
		"varying vec2 tc;\n"
		"void main() { tc = gl_MultiTexCoord0.xy;"
		" gl_Position = gl_Vertex; }\n";
	GLhandleARB p = glCreateProgramObjectARB();
	GLint ok = 0;
	char log[2048] = "";

	glAttachObjectARB(p, compile(GL_VERTEX_SHADER_ARB, vs));
	glAttachObjectARB(p, compile(GL_FRAGMENT_SHADER_ARB, fs));
	glLinkProgramARB(p);
	glGetObjectParameterivARB(p, GL_OBJECT_LINK_STATUS_ARB, &ok);
	if (!ok) {
		glGetInfoLogARB(p, sizeof log, NULL, log);
		printf("link failed: %s\n", log);
	}
	return p;
}

static GLuint mk(GLenum internal, GLenum format, int w, int h,
		 unsigned char v0, unsigned char v1, unsigned char v2)
{
	GLuint t;
	int n = format == GL_RGBA ? 4 : 1, i;
	unsigned char *buf = malloc(w * h * n);

	for (i = 0; i < w * h; i++) {
		if (n == 4) {
			buf[4 * i] = v0; buf[4 * i + 1] = v1;
			buf[4 * i + 2] = v2; buf[4 * i + 3] = 255;
		} else
			buf[i] = v0;
	}
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format,
		     GL_UNSIGNED_BYTE, buf);
	free(buf);
	return t;
}

static void quad(void)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
}

static void bind(int unit, GLuint t)
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, t);
}

static void centre(const char *what)
{
	unsigned char px[4] = { 0, 0, 0, 0 };

	glFinish();
	glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%-44s %02x %02x %02x (err 0x%x)\n", what, px[0], px[1], px[2],
	       (unsigned)glGetError());
	fflush(stdout);
}

static void display(void)
{
	GLhandleARB p;

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgramObjectARB(0);
	glEnable(GL_TEXTURE_2D);
	bind(0, tex_rgba);
	quad();
	centre("0 fixed pipeline, RGBA texture");

	glClear(GL_COLOR_BUFFER_BIT);
	p = program("#version 120\nvoid main() { gl_FragColor = vec4(1,0,0,1); }\n");
	glUseProgramObjectARB(p);
	quad();
	centre("1 shader, constant colour");

	glClear(GL_COLOR_BUFFER_BIT);
	p = program("#version 120\nuniform sampler2D Texture0;\nvarying vec2 tc;\n"
		    "void main() { gl_FragColor = texture2D(Texture0, tc); }\n");
	glUseProgramObjectARB(p);
	glUniform1iARB(glGetUniformLocationARB(p, "Texture0"), 0);
	bind(0, tex_rgba);
	quad();
	centre("2 shader, texture2D of RGBA");

	glClear(GL_COLOR_BUFFER_BIT);
	p = program("#version 120\nuniform sampler2D Texture0;\nvarying vec2 tc;\n"
		    "void main() { float y = texture2D(Texture0, tc).x;"
		    " gl_FragColor = vec4(y, y, y, 1); }\n");
	glUseProgramObjectARB(p);
	glUniform1iARB(glGetUniformLocationARB(p, "Texture0"), 0);
	bind(0, tex_y);
	quad();
	centre("3 shader, texture2D of GL_RED (expect 80)");

	glClear(GL_COLOR_BUFFER_BIT);
	p = program("#version 120\nuniform sampler2D Texture0;"
		    "uniform sampler2D Texture1;uniform sampler2D Texture2;\n"
		    "varying vec2 tc;\n"
		    "void main() { vec4 v = vec4(texture2D(Texture0, tc).x,"
		    " texture2D(Texture1, tc).x, texture2D(Texture2, tc).x, 1.0);"
		    " gl_FragColor = vec4(v.x, v.y, v.z, 1.0); }\n");
	glUseProgramObjectARB(p);
	glUniform1iARB(glGetUniformLocationARB(p, "Texture0"), 0);
	glUniform1iARB(glGetUniformLocationARB(p, "Texture1"), 1);
	glUniform1iARB(glGetUniformLocationARB(p, "Texture2"), 2);
	bind(0, tex_y); bind(1, tex_u); bind(2, tex_v);
	quad();
	centre("4 shader, three GL_RED textures (expect 80 40 c0)");

	/* 5: what vout_helper.c does: attributes from VBOs, an index buffer. */
	{
		static const GLfloat pos[] = { -1,-1,0, 1,-1,0, -1,1,0, 1,1,0 };
		static const GLfloat txc[] = { 0,0, 1,0, 0,1, 1,1 };
		static const GLushort idx[] = { 0, 1, 2, 2, 1, 3 };
		static const GLfloat ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		GLuint vbo[3];
		GLint ap, at;
		GLhandleARB vsh, fsh;
		const char *vs =
			"#version 120\nattribute vec4 MultiTexCoord0;"
			"attribute vec3 VertexPosition;uniform mat4 ProjectionMatrix;"
			"varying vec2 TexCoord0;\n"
			"void main() { TexCoord0 = MultiTexCoord0.st;"
			" gl_Position = ProjectionMatrix * vec4(VertexPosition, 1.0); }\n";
		const char *fs =
			"#version 120\nuniform sampler2D Texture0;"
			"varying vec2 TexCoord0;\n"
			"void main() { gl_FragColor = texture2D(Texture0, TexCoord0); }\n";

		glClear(GL_COLOR_BUFFER_BIT);
		vsh = compile(GL_VERTEX_SHADER_ARB, vs);
		fsh = compile(GL_FRAGMENT_SHADER_ARB, fs);
		p = glCreateProgramObjectARB();
		glAttachObjectARB(p, vsh);
		glAttachObjectARB(p, fsh);
		glLinkProgramARB(p);
		glUseProgramObjectARB(p);
		glUniform1iARB(glGetUniformLocationARB(p, "Texture0"), 0);
		glUniformMatrix4fvARB(glGetUniformLocationARB(p, "ProjectionMatrix"),
				      1, GL_FALSE, ident);
		bind(0, tex_rgba);
		glGenBuffersARB(3, vbo);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo[0]);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof txc, txc, GL_STATIC_DRAW_ARB);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo[1]);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof pos, pos, GL_STATIC_DRAW_ARB);
		glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, vbo[2]);
		glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, sizeof idx, idx,
				GL_STATIC_DRAW_ARB);
		at = glGetAttribLocationARB(p, "MultiTexCoord0");
		ap = glGetAttribLocationARB(p, "VertexPosition");
		printf("attribute locations: tex %d pos %d\n", at, ap);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo[0]);
		glEnableVertexAttribArrayARB(at);
		glVertexAttribPointerARB(at, 2, GL_FLOAT, 0, 0, 0);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo[1]);
		glEnableVertexAttribArrayARB(ap);
		glVertexAttribPointerARB(ap, 3, GL_FLOAT, 0, 0, 0);
		glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, vbo[2]);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
		centre("5 VBO attributes + glDrawElements");
	}

	/*
	 * 9, 10, 11: face culling, which VLC turns on (glEnable(0xb44)). The
	 * quad of mode 5 has counter-clockwise triangles: with back-face
	 * culling it must draw (9); mirrored in y it must be culled (10);
	 * and with the unsized GL_RED internal format VLC uses (11).
	 */
	{
		static const GLfloat pos[] = { -1,-1,0, 1,-1,0, -1,1,0, 1,1,0 };
		static const GLushort idx[] = { 0, 1, 2, 2, 1, 3 };
		static const GLfloat flip[16] = { 1,0,0,0, 0,-1,0,0, 0,0,1,0, 0,0,0,1 };
		static const GLfloat ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		const char *vs =
			"#version 120\nattribute vec3 VertexPosition;"
			"uniform mat4 M;varying vec2 tc;\n"
			"void main() { tc = VertexPosition.xy * 0.5 + 0.5;"
			" gl_Position = M * vec4(VertexPosition, 1.0); }\n";
		const char *fs =
			"#version 120\nuniform sampler2D Texture0;varying vec2 tc;\n"
			"void main() { gl_FragColor = texture2D(Texture0, tc); }\n";
		int k;
		GLuint tu;

		p = glCreateProgramObjectARB();
		glAttachObjectARB(p, compile(GL_VERTEX_SHADER_ARB, vs));
		glAttachObjectARB(p, compile(GL_FRAGMENT_SHADER_ARB, fs));
		glLinkProgramARB(p);
		glUseProgramObjectARB(p);
		glUniform1iARB(glGetUniformLocationARB(p, "Texture0"), 0);
		glEnableVertexAttribArrayARB(glGetAttribLocationARB(p, "VertexPosition"));
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
		glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
		glVertexAttribPointerARB(glGetAttribLocationARB(p, "VertexPosition"), 3,
					 GL_FLOAT, 0, 0, pos);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		for (k = 9; k <= 11; k++) {
			unsigned char px[4];

			if (k == 11) {
				glGenTextures(1, &tu);
				glBindTexture(GL_TEXTURE_2D, tu);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glTexImage2D(GL_TEXTURE_2D, 0, red ? GL_RED_ : GL_LUMINANCE, w, h, 0,
					     red ? GL_RED_ : GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
				{
					unsigned char *b = malloc(w * h);
					memset(b, 0x80, w * h);
					glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
							red ? GL_RED_ : GL_LUMINANCE,
							GL_UNSIGNED_BYTE, b);
					free(b);
				}
			} else
				bind(0, tex_rgba);
			glUniformMatrix4fvARB(glGetUniformLocationARB(p, "M"), 1, GL_FALSE,
					      k == 10 ? flip : ident);
			glEnable(GL_CULL_FACE);
			glClear(GL_COLOR_BUFFER_BIT);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
			glDisable(GL_CULL_FACE);
			glFinish();
			glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
			printf("%d cull face, %s: centre %02x %02x %02x (expect %s, err 0x%x)\n",
			       k, k == 9 ? "CCW" : k == 10 ? "y-mirrored" : "unsized GL_RED, CCW",
			       px[0], px[1], px[2],
			       k == 10 ? "00 00 00" : k == 9 ? "33 66 99" : "80 .. ..",
			       (unsigned)glGetError());
		}
		fflush(stdout);
	}

	/* 8: VLC's own fragment shader: uniform vec4 Coefficients[4], FillColor. */
	{
		static const GLfloat co[16] = {
			1.0f, 1.0f, 1.0f, 0.0f,      /* Coefficients[0], for plane 0 */
			0.0f, -0.344f, 1.772f, 0.0f, /* [1] */
			1.402f, -0.714f, 0.0f, 0.0f, /* [2] */
			-0.7f, 0.53f, -0.89f, 0.0f   /* [3], added once */
		};
		const char *fs =
			"#version 120\nvarying vec2 TexCoord0;varying vec2 TexCoord1;"
			"varying vec2 TexCoord2;"
			"uniform sampler2D Texture0;uniform sampler2D Texture1;"
			"uniform sampler2D Texture2;uniform vec4 Coefficients[4];"
			"uniform vec4 FillColor;\n"
			"void main(void) { float val; vec4 colors;\n"
			" colors = texture2D(Texture0, TexCoord0); val = colors.x;"
			" vec4 color0 = vec4(val, val, val, 1);\n"
			" colors = texture2D(Texture1, TexCoord1); val = colors.x;"
			" vec4 color1 = vec4(val, val, val, 1);\n"
			" colors = texture2D(Texture2, TexCoord2); val = colors.x;"
			" vec4 color2 = vec4(val, val, val, 1);\n"
			" vec4 result = (color0 * Coefficients[0]) + Coefficients[3];\n"
			" result = (color1 * Coefficients[1]) + result;\n"
			" result = (color2 * Coefficients[2]) + result;\n"
			" gl_FragColor = result * FillColor; }\n";
		const char *vs =
			"#version 120\nvarying vec2 TexCoord0;varying vec2 TexCoord1;"
			"varying vec2 TexCoord2;\n"
			"void main() { TexCoord0 = gl_MultiTexCoord0.st;"
			" TexCoord1 = gl_MultiTexCoord0.st;"
			" TexCoord2 = gl_MultiTexCoord0.st; gl_Position = gl_Vertex; }\n";
		float y = 0x80 / 255.0f, u = 0x40 / 255.0f, v = 0xc0 / 255.0f, e[3];
		int i;
		unsigned char px[4];

		p = glCreateProgramObjectARB();
		glAttachObjectARB(p, compile(GL_VERTEX_SHADER_ARB, vs));
		glAttachObjectARB(p, compile(GL_FRAGMENT_SHADER_ARB, fs));
		glLinkProgramARB(p);
		glUseProgramObjectARB(p);
		glUniform1iARB(glGetUniformLocationARB(p, "Texture0"), 0);
		glUniform1iARB(glGetUniformLocationARB(p, "Texture1"), 1);
		glUniform1iARB(glGetUniformLocationARB(p, "Texture2"), 2);
		glUniform4fvARB(glGetUniformLocationARB(p, "Coefficients"), 4, co);
		glUniform4fARB(glGetUniformLocationARB(p, "FillColor"), 1, 1, 1, 1);
		glDisable(GL_TEXTURE_2D);
		bind(0, tex_y); bind(1, tex_u); bind(2, tex_v);
		glClear(GL_COLOR_BUFFER_BIT);
		quad();
		glFinish();
		glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		for (i = 0; i < 3; i++) {
			e[i] = y * co[i] + u * co[4 + i] + v * co[8 + i] + co[12 + i];
			e[i] = e[i] < 0 ? 0 : e[i] > 1 ? 1 : e[i];
		}
		printf("8 VLC shader, Coefficients[4]: got %02x %02x %02x, expect "
		       "%02x %02x %02x (err 0x%x)\n", px[0], px[1], px[2],
		       (int)(e[0] * 255 + 0.5f), (int)(e[1] * 255 + 0.5f),
		       (int)(e[2] * 255 + 0.5f), (unsigned)glGetError());
		fflush(stdout);
	}

	/*
	 * 6, 7: VLC uploads a plane padded to a longer row (pitch 768 for 704
	 * visible), with GL_UNPACK_ROW_LENGTH, from client memory (6) or from a
	 * pixel unpack buffer (7). Visible bytes 0x80, padding 0xff: a row
	 * length that is ignored shows 0xff or a shifted picture.
	 */
	{
		int pitch = ((w + 255) / 256) * 256, x, y, mode;
		unsigned char *buf = malloc(pitch * h);
		GLuint t, pbo;
		unsigned char px[4];

		for (y = 0; y < h; y++)
			for (x = 0; x < pitch; x++)
				buf[y * pitch + x] = x < w ? 0x80 : 0xff;
		glGenTextures(1, &t);
		glGenBuffersARB(1, &pbo);
		for (mode = 6; mode <= 7; mode++) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, t);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, red ? GL_R8_ : GL_LUMINANCE, w, h, 0,
				     red ? GL_RED_ : GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
			if (mode == 6) {
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
						red ? GL_RED_ : GL_LUMINANCE,
						GL_UNSIGNED_BYTE, buf);
			} else {
				glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pbo);
				glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, pitch * h, NULL,
						GL_STREAM_DRAW_ARB);
				glBufferSubDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0, pitch * h, buf);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
						red ? GL_RED_ : GL_LUMINANCE,
						GL_UNSIGNED_BYTE, NULL);
				glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			}
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glUseProgramObjectARB(0);
			glClear(GL_COLOR_BUFFER_BIT);
			glEnable(GL_TEXTURE_2D);
			quad();
			glFinish();
			glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
			printf("%d row length %d, %s: centre %02x ", mode, pitch,
			       mode == 6 ? "client memory" : "unpack buffer", px[0]);
			glReadPixels(300, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
			printf("(300,10) %02x ", px[0]);
			glReadPixels(5, 230, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
			printf("(5,230) %02x (expect 80 everywhere, err 0x%x)\n", px[0],
			       (unsigned)glGetError());
			fflush(stdout);
		}
	}

	if (hold) {
		int a;

		glUseProgramObjectARB(0);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_BLEND);
		for (a = 0; a <= 1; a++) {
			glClearColor(0, 1, 0, (float)a);
			glClear(GL_COLOR_BUFFER_BIT);
			glutSwapBuffers();
			printf("showing solid green with alpha %d for 8 s\n", a);
			fflush(stdout);
			sleep(8);
		}
	}
	exit(0);
}

int main(int argc, char **argv)
{


	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
	glutInitWindowSize(320, 240);
	glutCreateWindow("glshader");
	printf("GL_RENDERER: %s\nGL_VERSION:  %s\n", glGetString(GL_RENDERER),
	       glGetString(GL_VERSION));
	red = strstr((const char *)glGetString(GL_EXTENSIONS),
		     "GL_ARB_texture_rg") != NULL;
	printf("GL_ARB_texture_rg: %s\n", red ? "yes" : "no");
	hold = argc > 3;
	if (argc > 2) {
		w = atoi(argv[1]);
		h = atoi(argv[2]);
	}
	printf("texture size %dx%d (chroma %dx%d)\n", w, h, w / 2, h / 2);
	tex_rgba = mk(GL_RGBA, GL_RGBA, w, h, 0x33, 0x66, 0x99);
	tex_y = mk(red ? GL_R8_ : GL_LUMINANCE, red ? GL_RED_ : GL_LUMINANCE,
		   w, h, 0x80, 0, 0);
	tex_u = mk(red ? GL_R8_ : GL_LUMINANCE, red ? GL_RED_ : GL_LUMINANCE,
		   w / 2, h / 2, 0x40, 0, 0);
	tex_v = mk(red ? GL_R8_ : GL_LUMINANCE, red ? GL_RED_ : GL_LUMINANCE,
		   w / 2, h / 2, 0xc0, 0, 0);
	glutDisplayFunc(display);
	glutMainLoop();
	return 0;
}
