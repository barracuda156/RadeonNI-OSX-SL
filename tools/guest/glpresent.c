/*
 * glpresent: which ways of drawing reach the screen?
 *
 *   glpresent [seconds per phase]
 *
 * A 400x300 window, double buffered, that cycles through draws of a solid
 * colour, 4 s each (printed as they start), so that the screen can be grabbed
 * in each:
 *   0 glClear, green
 *   1 fixed pipeline, glBegin quad, blue
 *   2 shader, glDrawArrays from client memory, red
 *   3 shader, VBO attributes, glDrawElements, yellow
 *   4 the same as 3, with VLC's state: cull face on, depth mask off, alpha 0
 * Build in the guest:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o glpresent glpresent.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <GLUT/glut.h>

static GLhandleARB prog;
static GLuint vbo, ibo;
static int phase = -1, secs = 4;
static double t0;

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static GLhandleARB sh(GLenum type, const char *src)
{
	GLhandleARB s = glCreateShaderObjectARB(type);

	glShaderSourceARB(s, 1, &src, NULL);
	glCompileShaderARB(s);
	return s;
}

static void init(void)
{
	static const GLfloat pos[] = { -0.8f,-0.8f,0, 0.8f,-0.8f,0, -0.8f,0.8f,0, 0.8f,0.8f,0 };
	static const GLushort idx[] = { 0, 1, 2, 2, 1, 3 };

	prog = glCreateProgramObjectARB();
	glAttachObjectARB(prog, sh(GL_VERTEX_SHADER_ARB,
		"#version 120\nattribute vec3 VertexPosition;\n"
		"void main() { gl_Position = vec4(VertexPosition, 1.0); }\n"));
	glAttachObjectARB(prog, sh(GL_FRAGMENT_SHADER_ARB,
		"#version 120\nuniform vec4 Color;\n"
		"void main() { gl_FragColor = Color; }\n"));
	glLinkProgramARB(prog);
	glGenBuffersARB(1, &vbo);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof pos, pos, GL_STATIC_DRAW_ARB);
	glGenBuffersARB(1, &ibo);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, ibo);
	glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, sizeof idx, idx, GL_STATIC_DRAW_ARB);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
}

static void display(void)
{
	static const GLfloat tri[] = { -0.8f,-0.8f,0, 0.8f,-0.8f,0, -0.8f,0.8f,0, 0.8f,0.8f,0 };
	int p = (int)((now() - t0) / secs), at;

	if (p >= 5)
		exit(0);
	if (p != phase) {
		phase = p;
		printf("phase %d\n", p);
		fflush(stdout);
	}
	glClearColor(0.2f, 0.2f, 0.2f, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgramObjectARB(0);
	at = glGetAttribLocationARB(prog, "VertexPosition");
	if (p == 0) {
		glClearColor(0, 1, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
	} else if (p == 1) {
		glColor3f(0, 0, 1);
		glBegin(GL_QUADS);
		glVertex2f(-0.8f, -0.8f); glVertex2f(0.8f, -0.8f);
		glVertex2f(0.8f, 0.8f); glVertex2f(-0.8f, 0.8f);
		glEnd();
	} else if (p == 2) {
		glUseProgramObjectARB(prog);
		glUniform4fARB(glGetUniformLocationARB(prog, "Color"), 1, 0, 0, 1);
		glEnableVertexAttribArrayARB(at);
		glVertexAttribPointerARB(at, 3, GL_FLOAT, 0, 0, tri);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	} else {
		glUseProgramObjectARB(prog);
		glUniform4fARB(glGetUniformLocationARB(prog, "Color"), 1, 1, 0, p == 4 ? 0 : 1);
		if (p == 4) {
			glEnable(GL_CULL_FACE);
			glDepthMask(GL_FALSE);
		}
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo);
		glEnableVertexAttribArrayARB(at);
		glVertexAttribPointerARB(at, 3, GL_FLOAT, 0, 0, 0);
		glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, ibo);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
		glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
		glDisable(GL_CULL_FACE);
	}
	glutSwapBuffers();
}

static void idle(void)
{
	glutPostRedisplay();
}

int main(int argc, char **argv)
{
	glutInit(&argc, argv);
	if (argc > 1)
		secs = atoi(argv[1]);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
	glutInitWindowSize(400, 300);
	glutInitWindowPosition(200, 200);
	glutCreateWindow("glpresent");
	init();
	t0 = now();
	glutDisplayFunc(display);
	glutIdleFunc(idle);
	glutMainLoop();
	return 0;
}
