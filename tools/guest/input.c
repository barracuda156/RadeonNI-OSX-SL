/*
 * input: post keyboard and mouse events from inside the Mac, to drive a
 * program (a game's menus) when nobody is at the keyboard.
 *
 *   input key CODE [CHAR]        virtual key code (and its ASCII char), down then up
 *   input click X Y [hold_ms]    move there, press, release (desktop coords)
 *   input move X Y
 *   input wait MS
 *
 * Several commands may follow one another: input click 400 300 wait 500 key 36
 * Virtual key codes (ANSI) with characters: return 36 13, escape 53 27,
 * space 49 32, tab 48 9, arrows left 123 28, right 124 29, up 126 30,
 * down 125 31; letters a 0 97, s 1 115, d 2 100, w 13 119.
 *
 * Build:
 *   gcc -Wall -o input input.c -framework ApplicationServices
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ApplicationServices/ApplicationServices.h>

static void key(int code, int ch)
{
	CGPostKeyboardEvent((CGCharCode)ch, (CGKeyCode)code, true);
	usleep(80 * 1000);
	CGPostKeyboardEvent((CGCharCode)ch, (CGKeyCode)code, false);
	usleep(80 * 1000);
}

static void move(int x, int y)
{
	CGPoint p = CGPointMake(x, y);

	CGWarpMouseCursorPosition(p);
	CGPostMouseEvent(p, true, 1, false);
	usleep(40 * 1000);
}

static void click(int x, int y, int hold_ms)
{
	CGPoint p = CGPointMake(x, y);

	move(x, y);
	CGPostMouseEvent(p, true, 1, true);
	usleep((hold_ms > 0 ? hold_ms : 80) * 1000);
	CGPostMouseEvent(p, true, 1, false);
	usleep(80 * 1000);
}

int main(int argc, char **argv)
{
	int i = 1;

	if (argc < 2) {
		fprintf(stderr, "usage: input key CODE [ms] | click X Y [ms] | move X Y | wait MS ...\n");
		return 1;
	}
	while (i < argc) {
		if (!strcmp(argv[i], "key") && i + 1 < argc) {
			int ch = 0, used = 2;

			/* an optional second number is the character code */
			if (i + 2 < argc && argv[i + 2][0] >= '0' && argv[i + 2][0] <= '9') {
				ch = atoi(argv[i + 2]);
				used = 3;
			}
			key(atoi(argv[i + 1]), ch);
			i += used;
		} else if (!strcmp(argv[i], "click") && i + 2 < argc) {
			click(atoi(argv[i + 1]), atoi(argv[i + 2]), 0);
			i += 3;
		} else if (!strcmp(argv[i], "move") && i + 2 < argc) {
			move(atoi(argv[i + 1]), atoi(argv[i + 2]));
			i += 3;
		} else if (!strcmp(argv[i], "wait") && i + 1 < argc) {
			usleep(atoi(argv[i + 1]) * 1000);
			i += 2;
		} else {
			fprintf(stderr, "input: bad argument %s\n", argv[i]);
			return 1;
		}
	}
	return 0;
}
