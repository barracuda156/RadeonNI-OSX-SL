/*
 * See rdn_arbtext.h.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include "rdn_arbtext.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LITERALS 64
#define NAME_MAX_LEN 48

struct literal {
	char name[NAME_MAX_LEN];
	double v[4];
};

static int is_id(int c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '$';
}

/* Skip blanks and tabs. */
static const char *blank(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t'))
		p++;
	return p;
}

/* An identifier at p: its length, 0 when there is none. */
static size_t ident(const char *p, const char *end)
{
	size_t n = 0;

	if (p < end && !isdigit((unsigned char)*p))
		while (p + n < end && is_id(p[n]))
			n++;
	return n;
}

static int word(const char *p, const char *end, const char *w)
{
	size_t n = strlen(w);

	return p + n <= end && !strncmp(p, w, n) && (p + n == end || !is_id(p[n]));
}

/* "PARAM name = { a, b, c, d };" -> the literal, 0 if it is not one. */
static int parse_literal(const char *p, const char *end, struct literal *lit)
{
	size_t n;
	char tmp[256], *q, *e;
	int i;

	p = blank(p, end);
	if (!word(p, end, "PARAM"))
		return 0;
	p = blank(p + 5, end);
	n = ident(p, end);
	if (!n || n >= NAME_MAX_LEN)
		return 0;
	memcpy(lit->name, p, n);
	lit->name[n] = 0;
	p = blank(p + n, end);
	if (p >= end || *p != '=')
		return 0;
	p = blank(p + 1, end);
	if (p >= end || *p != '{')
		return 0;
	p++;
	for (n = 0; p + n < end && p[n] != '}' && n < sizeof(tmp) - 1; n++)
		tmp[n] = p[n];
	if (p + n >= end || p[n] != '}')
		return 0;
	tmp[n] = 0;
	lit->v[0] = lit->v[1] = 0;
	lit->v[2] = 0;
	lit->v[3] = 1;
	q = tmp;
	for (i = 0; i < 4; i++) {
		double d = strtod(q, &e);

		if (e == q)
			break;
		lit->v[i] = d;
		q = e;
		while (*q == ' ' || *q == '\t')
			q++;
		if (*q == ',')
			q++;
		else
			break;
	}
	return i > 0 || e != tmp;
}

static int component(char c)
{
	switch (c) {
	case 'x': case 'r': case 's': return 0;
	case 'y': case 'g': case 't': return 1;
	case 'z': case 'b': case 'p': return 2;
	case 'w': case 'a': case 'q': return 3;
	}
	return -1;
}

/*
 * "ALIAS name = rhs;" in [p, end): what it becomes, appended to out
 * (capacity checked by the caller through the worst-case size). Returns
 * the number of bytes written, 0 for "leave it".
 */
static size_t rewrite_alias(const char *p, const char *end,
			    const struct literal *lits, int nlits, char *out)
{
	const char *name, *rhs, *semi;
	size_t nlen, i;
	const char *kw = NULL;

	p = blank(p, end);
	if (!word(p, end, "ALIAS"))
		return 0;
	p = blank(p + 5, end);
	name = p;
	nlen = ident(p, end);
	if (!nlen)
		return 0;
	p = blank(p + nlen, end);
	if (p >= end || *p != '=')
		return 0;
	rhs = blank(p + 1, end);
	semi = memchr(rhs, ';', end - rhs);
	if (!semi)
		return 0;

	if (!strncmp(rhs, "vertex.", 7) || !strncmp(rhs, "fragment.", 9))
		kw = "ATTRIB";
	else if (!strncmp(rhs, "program.", 8) || !strncmp(rhs, "state.", 6))
		kw = "PARAM";
	else if (!strncmp(rhs, "result.", 7))
		kw = "OUTPUT";
	if (kw)
		return (size_t)sprintf(out, "%s %.*s = %.*s;", kw, (int)nlen, name,
				       (int)(semi - rhs), rhs);

	/* name.c of a literal PARAM */
	{
		size_t rl = ident(rhs, semi);

		if (rl && rhs[rl] == '.') {
			const char *s = rhs + rl + 1;
			int c = s < semi ? component(*s) : -1;

			if (c >= 0 && blank(s + 1, semi) == semi) {
				for (i = 0; i < (size_t)nlits; i++)
					if (strlen(lits[i].name) == rl &&
					    !memcmp(lits[i].name, rhs, rl)) {
						double v = lits[i].v[c];

						return (size_t)sprintf(out,
							"PARAM %.*s = { %.9g, %.9g, %.9g, %.9g };",
							(int)nlen, name, v, v, v, v);
					}
			}
		}
	}
	return 0;
}

char *rdn_arb_alias_rewrite(const char *text, int *len)
{
	struct literal lits[MAX_LITERALS];
	int nlits = 0, changed = 0;
	const char *end, *line;
	char *out;
	size_t cap, o = 0;

	if (!text || !len || *len <= 0)
		return NULL;
	end = text + *len;
	/* Each rewritten statement is at most 80 bytes more than itself. */
	{
		size_t nl = 0;
		const char *q;

		for (q = text; q < end; q++)
			if (*q == '\n')
				nl++;
		cap = (size_t)*len + (nl + 1) * 96 + 1;
	}
	out = malloc(cap);
	if (!out)
		return NULL;

	for (line = text; line < end; ) {
		const char *nl = memchr(line, '\n', end - line);
		const char *le = nl ? nl : end;
		const char *s = blank(line, le);
		size_t w = 0;

		if (s < le && *s != '#') {
			struct literal lit;

			if (parse_literal(s, le, &lit) && nlits < MAX_LITERALS)
				lits[nlits++] = lit;
			w = rewrite_alias(s, le, lits, nlits, out + o + (s - line));
			if (w) {
				const char *semi = memchr(s, ';', le - s);

				memcpy(out + o, line, s - line);
				o += (s - line) + w;
				/* what follows the statement on its line */
				memcpy(out + o, semi + 1, le - (semi + 1));
				o += le - (semi + 1);
				changed = 1;
				if (nl)
					out[o++] = '\n';
				line = nl ? nl + 1 : end;
				continue;
			}
		}
		memcpy(out + o, line, le - line);
		o += le - line;
		if (nl)
			out[o++] = '\n';
		line = nl ? nl + 1 : end;
	}
	if (!changed) {
		free(out);
		return NULL;
	}
	out[o] = 0;
	*len = (int)o;
	return out;
}
